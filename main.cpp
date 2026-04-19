// main.cpp — bare-metal video feedback with toggleable layers.
//
// Architecture:
//   • Each processing layer is its own .glsl file under shaders/layers/.
//   • main.frag orchestrates them, gated by a uEnable bitfield.
//   • Host loads and #include-resolves main.frag at startup (and on reload).
//   • Two fields (A and B) each have ping-pong RGBA16F textures. When the
//     COUPLE layer is on, both fields update and each samples the other.
//   • Camera (V4L2) is uploaded to a texture that the EXTERNAL layer mixes in.
//
// Build: see Makefile.  Run: ./feedback from the linux/ directory (so the
// shader paths resolve).

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <timeapi.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <chrono>
#include <thread>

#include "camera.h"
#include "recorder.h"
#include "overlay.h"

// ── runtime config (from CLI) ─────────────────────────────────────────────
struct Cfg {
    int  simW = 0, simH = 0;        // 0 means "match display"
    int  dispW = 1280, dispH = 720;
    bool fullscreen = false;
    int  precision = 32;            // 16 or 32 (RGBA16F vs RGBA32F)
    int  blurQ = 1;                 // 0=5-tap  1=9-tap gauss  2=25-tap gauss
    int  caQ   = 1;                 // 0=3-sample  1=5-ramp  2=8-wavelen
    int  noiseQ = 1;                // 0=white  1=pink
    int  fields = 2;                // 1..4 coupled fields
    int  iters = 1;                 // feedback iterations per displayed frame
    bool vsync = true;
    int  playerFps = 60;            // hard cap on display loop; 0 = uncapped
    int  recFps = 0;                // 0 = follow playerFps (or 60 if uncapped)
    // Recorder RAM / compression knobs — passed through to Recorder::Config.
    int  recRamMB = 0;              // 0 = auto (min(free/4, 8 GB))
    int  recEncoders = 0;           // 0 = auto (hw_concurrency - 2, clamped)
    bool recUncompressed = false;   // write uncompressed EXR
};

static void print_cli_help() {
    printf(
      "Usage: feedback.exe [options]\n"
      "  --sim-res WxH       internal simulation resolution (default: match display)\n"
      "  --display-res WxH   window size in windowed mode (default: 1280x720)\n"
      "  --fullscreen        borderless fullscreen at monitor's native resolution\n"
      "  --precision 16|32   feedback buffer float precision (default: 32)\n"
      "  --blur-q 0|1|2      blur kernel: 5-tap / 9-tap / 25-tap gaussian (default: 1)\n"
      "  --ca-q   0|1|2      chromatic aberration: 3 / 5 / 8 samples (default: 1)\n"
      "  --noise-q 0|1       sensor noise: white / pink-1/f (default: 1)\n"
      "  --fields 1|2|3|4    coupled feedback fields (default: 2)\n"
      "  --iters N           feedback iterations per displayed frame, 1-32 (default: 1)\n"
      "  --vsync on|off      vsync (default: on)\n"
      "  --fps N             cap display loop to N fps, 0=uncapped (default: 60)\n"
      "  --rec-fps N         recording framerate metadata tag (default: follow --fps)\n"
      "  --rec-ram-gb N      RAM buffer for recording, GB (default: auto, capped at 8)\n"
      "  --rec-encoders N    encoder threads for EXR writes (default: auto)\n"
      "  --rec-uncompressed  write uncompressed EXR — larger files, much faster writes\n"
      "  -h, --help          show this help\n\n"
      "Examples:\n"
      "  feedback --fullscreen\n"
      "  feedback --fullscreen --sim-res 3840x2160 --precision 32\n"
      "  feedback --fields 4 --blur-q 2 --ca-q 2  (all quality maxed)\n");
}

static Cfg parse_cli(int argc, char** argv) {
    Cfg c;
    for (int i = 1; i < argc; i++) {
        auto eq = [&](const char* n) { return strcmp(argv[i], n) == 0; };
        auto next = [&]() -> const char* { return (i+1 < argc) ? argv[++i] : ""; };
        if      (eq("--sim-res"))      { sscanf(next(), "%dx%d", &c.simW, &c.simH); }
        else if (eq("--display-res"))  { sscanf(next(), "%dx%d", &c.dispW, &c.dispH); }
        else if (eq("--fullscreen"))   { c.fullscreen = true; }
        else if (eq("--precision"))    { int p = atoi(next()); if (p==16||p==32) c.precision = p; }
        else if (eq("--blur-q"))       { int q = atoi(next()); if (q>=0&&q<=2) c.blurQ = q; }
        else if (eq("--ca-q"))         { int q = atoi(next()); if (q>=0&&q<=2) c.caQ = q; }
        else if (eq("--noise-q"))      { int q = atoi(next()); if (q>=0&&q<=1) c.noiseQ = q; }
        else if (eq("--fields"))       { int f = atoi(next()); if (f>=1&&f<=4) c.fields = f; }
        else if (eq("--iters"))        { c.iters = atoi(next()); if (c.iters < 1) c.iters = 1;
                                          if (c.iters > 32) c.iters = 32; }
        else if (eq("--vsync"))        { c.vsync = (strcmp(next(), "on") == 0); }
        else if (eq("--fps"))          { c.playerFps = atoi(next()); if (c.playerFps < 0) c.playerFps = 0; }
        else if (eq("--rec-fps"))      { c.recFps = atoi(next()); if (c.recFps < 1) c.recFps = 60; }
        else if (eq("--rec-ram-gb"))   { int g = atoi(next()); if (g > 0) c.recRamMB = g * 1024; }
        else if (eq("--rec-encoders")) { int n = atoi(next()); if (n > 0) c.recEncoders = n; }
        else if (eq("--rec-uncompressed")) { c.recUncompressed = true; }
        else if (eq("-h") || eq("--help")) { print_cli_help(); exit(0); }
        else { fprintf(stderr, "[cli] unknown arg: %s\n", argv[i]); print_cli_help(); exit(1); }
    }
    // If user didn't pass --rec-fps, use the player cap (or 60 when uncapped).
    if (c.recFps == 0) c.recFps = c.playerFps > 0 ? c.playerFps : 60;
    return c;
}

// File-scope cfg, so helper functions declared before State can reference it.
static Cfg g_cfg;

// ── layer enum (must mirror main.frag) ────────────────────────────────────
enum : int {
    L_WARP     = 1<<0,
    L_OPTICS   = 1<<1,
    L_GAMMA    = 1<<2,
    L_COLOR    = 1<<3,
    L_CONTRAST = 1<<4,
    L_DECAY    = 1<<5,
    L_NOISE    = 1<<6,
    L_COUPLE   = 1<<7,
    L_EXTERNAL = 1<<8,
    L_INJECT   = 1<<9,
    L_PHYSICS  = 1<<10,
    L_THERMAL  = 1<<11,
    L_ALL      = (1<<12) - 1
};

struct LayerInfo { const char* name; int bit; int fkey; };
static const LayerInfo LAYERS[] = {
    {"warp",     L_WARP,     GLFW_KEY_F1},
    {"optics",   L_OPTICS,   GLFW_KEY_F2},
    {"gamma",    L_GAMMA,    GLFW_KEY_F3},
    {"color",    L_COLOR,    GLFW_KEY_F4},
    {"contrast", L_CONTRAST, GLFW_KEY_F5},
    {"decay",    L_DECAY,    GLFW_KEY_F6},
    {"noise",    L_NOISE,    GLFW_KEY_F7},
    {"couple",   L_COUPLE,   GLFW_KEY_F8},
    {"external", L_EXTERNAL, GLFW_KEY_F9},
    {"inject",   L_INJECT,   GLFW_KEY_F10},
    {"physics",  L_PHYSICS,  GLFW_KEY_INSERT},
    {"thermal",  L_THERMAL,  GLFW_KEY_PAGE_DOWN},
};

// ── per-layer parameters ──────────────────────────────────────────────────
struct Params {
    // warp
    float zoom = 1.010f, theta = 0.010f;
    float pivotX = 0.5f, pivotY = 0.5f;
    float transX = 0.0f, transY = 0.0f;
    // optics
    float chroma = 0.002f;
    float blurX = 1.0f, blurY = 1.0f, blurAngle = 0.0f;
    // gamma
    float gamma = 2.2f;
    // color
    float hueRate = 0.003f, satGain = 1.010f;
    // contrast
    float contrast = 1.020f;
    // decay
    float decay = 0.995f;
    // noise
    float noise = 0.002f;
    // couple
    float couple = 0.05f;
    // external
    float external = 0.20f;
    // inject
    float inject = 0.0f;
    int   pattern = 0;
    // physics (Crutchfield camera-side knobs)
    int   invert      = 0;       // 0 = off, 1 = on (Crutchfield's "s")
    float sensorGamma = 1.0f;    // 1.0 = no-op; Crutchfield: 0.6..0.9
    float satKnee     = 0.0f;    // 0 = hard clip; 1 = full Reinhard
    float colorCross  = 0.0f;    // 0 = independent RGB; 1 = full averaging
    // thermal (air turbulence between camera and monitor)
    float thermAmp    = 0.015f;  // modest shimmer by default
    float thermScale  = 4.0f;    // mid-scale rolls
    float thermSpeed  = 2.0f;    // gentle evolution
    float thermRise   = 0.5f;    // some vertical advection
    float thermSwirl  = 0.3f;    // subtle vortical bias
};

// ── framebuffer-object helper ─────────────────────────────────────────────
struct FBO { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };

static void resize_fbo(FBO& f, int w, int h) {
    if (!f.fbo) glGenFramebuffers(1, &f.fbo);
    if (!f.tex) glGenTextures(1, &f.tex);
    f.w = w; f.h = h;
    glBindTexture(GL_TEXTURE_2D, f.tex);
    GLenum internalFmt = (g_cfg.precision == 32) ? GL_RGBA32F : GL_RGBA16F;
    GLenum type        = (g_cfg.precision == 32) ? GL_FLOAT   : GL_HALF_FLOAT;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, GL_RGBA, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, f.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void clear_fbo(FBO& f) {
    glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ── shader loading with #include resolution ───────────────────────────────
// Look for shaders in CWD first, then next to the executable. This lets the
// app be launched by double-clicking on Windows as well as from the terminal.
static std::string g_shader_base = "";

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        // Retry relative to the exe dir if we've got one.
        if (!g_shader_base.empty()) {
            std::ifstream f2(g_shader_base + path);
            if (f2) { std::stringstream ss; ss << f2.rdbuf(); return ss.str(); }
        }
        fprintf(stderr, "can't open %s\n", path.c_str()); exit(1);
    }
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

// Resolve  #include "relative/path"  recursively, relative to the file's dir.
static std::string resolve_includes(const std::string& path, int depth = 0) {
    if (depth > 8) { fprintf(stderr, "include depth exceeded\n"); exit(1); }
    std::string src = read_file(path);
    std::string dir = path.substr(0, path.find_last_of('/') + 1);

    std::string out; out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (src.compare(i, 9, "#include ") == 0) {
            size_t q1 = src.find('"', i);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos
                                                  : src.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string inc = src.substr(q1 + 1, q2 - q1 - 1);
                // #includes in layer files are relative to shaders/, not to the layer file
                std::string base = "shaders/";
                out += "// ── included: " + inc + " ──\n";
                out += resolve_includes(base + inc, depth + 1);
                out += "\n";
                size_t eol = src.find('\n', q2);
                i = (eol == std::string::npos) ? src.size() : eol + 1;
                continue;
            }
        }
        size_t eol = src.find('\n', i);
        if (eol == std::string::npos) { out.append(src, i, std::string::npos); break; }
        out.append(src, i, eol - i + 1);
        i = eol + 1;
    }
    (void)dir;
    return out;
}

// Returns 0 on failure so callers (notably hot-reload) can keep the old
// program alive instead of terminating the process on a bad edit.
static GLuint compile_shader(GLenum type, const std::string& src, const char* tag) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "[%s] compile error:\n%s\n", tag, log);
        std::istringstream is(src); std::string line; int n = 1;
        while (std::getline(is, line))
            fprintf(stderr, "%4d | %s\n", n++, line.c_str());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs, const char* tag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fprintf(stderr, "[%s] link error: %s\n", tag, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint build_feedback_program() {
    std::string vs_src = read_file("shaders/main.vert");
    std::string fs_src = resolve_includes("shaders/main.frag");

    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src, "main.vert");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, "main.frag");
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint p = link_program(vs, fs, "feedback");
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

static GLuint build_blit_program() {
    std::string vs_src = read_file("shaders/main.vert");
    std::string fs_src = read_file("shaders/blit.frag");
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src, "main.vert");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, "blit.frag");
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint p = link_program(vs, fs, "blit");
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ── global state ──────────────────────────────────────────────────────────
struct State {
    int  winW = 1280, winH = 720;      // current window / display size
    int  simW = 1280, simH = 720;      // simulation resolution (FBO size)

    // Up to 4 feedback fields. Each is a ping-pong pair.
    // Fields beyond `activeFields` are unused (not created).
    FBO field[4][2];                   // field[i][0] and field[i][1]
    bool writeA = true;                // toggles which slot we're writing to

    int  enable = L_WARP | L_OPTICS | L_COLOR | L_CONTRAST
                | L_DECAY | L_NOISE | L_INJECT;
    Params p;
    bool paused = false;
    int  selectedLayer = 0;
    bool needClear = true;
    uint32_t frame = 0;

    // Runtime quality toggles (initialised from g_cfg in main()).
    int  blurQ = 1, caQ = 1, noiseQ = 1;
    int  activeFields = 2;             // 1..4; 1 = no coupling, 4 = full CML

    Camera  cam;
    GLuint  camTex = 0;
    std::vector<uint8_t> camBuf;
    bool    camReady = false;

    Recorder rec;
    Overlay  ov;
    // List of recording directories created this session (filled when each
    // recording stops). Used by the post-exit ffmpeg-encode prompt.
    std::vector<std::string> recordingsThisSession;

    // For runtime fullscreen toggle (F2 conflicts with optics layer; we use
    // F-key handled via separate path. See key_cb.) Stash GLFW window pointer
    // and the windowed-mode rect so we can restore on toggle-back.
    GLFWwindow* win = nullptr;
    bool isFullscreen = false;
    int  savedX = 100, savedY = 100, savedW = 1280, savedH = 720;
};
static State S;

// ── help text ─────────────────────────────────────────────────────────────
static void print_help() {
    printf(
      "\n=== video feedback ===\n"
      " F1..F10  toggle layer (warp, optics, gamma, color, contrast,\n"
      "          decay, noise, couple, external, inject)\n"
      " F11      toggle fullscreen / windowed\n"
      " PgUp     cycle blur kernel (5-tap / 9-gauss / 25-gauss)\n"
      " F12      cycle chromatic aberration (3 / 5 / 8 samples)\n"
      " Home     cycle noise (white / pink 1/f)\n"
      " End      cycle coupled fields (1 / 2 / 3 / 4)\n"
      " H        toggle help overlay (in-window)\n"
      " `        start/stop recording (feedback_<timestamp>.mp4)\n"
      " \\        reload shaders from disk\n"
      " Ctrl+S   save current settings as preset (./presets/auto_*.ini)\n"
      " Ctrl+N   load next preset      Ctrl+P  load previous preset\n"
      " C        clear all fields to black\n"
      " P        pause/resume\n"
      " SPACE    inject current pattern (hold)\n"
      " 1..5     pattern: H-bars, V-bars, dot, checker, gradient\n"
      " Esc      quit\n\n"
      " --- parameter adjustments (hold Shift for 20x coarse steps) ---\n"
      " Q/A   zoom            W/S   rotation       arrows  translate\n"
      " [/]   chroma         ;/'   blur-X          ,/.   blur-Y\n"
      " -/=   blur-angle     G/B   gamma\n"
      " E/D   hue rate       R/F   saturation\n"
      " T/Y   contrast       U/J   decay           N/M   noise\n"
      " K/I   couple amt     O/L   external amt\n"
      " V     invert (toggle)  Z/X  sensor-gamma  7/8 sat-knee  9/0 color-xtalk\n"
      " Ins   toggle physics layer   PgDn  toggle thermal layer\n"
      " Numpad thermal: 1/4 amp   2/5 scale   3/6 speed   7/8 rise   9/0 swirl\n\n");
    fflush(stdout);
}

static void print_status() {
    const auto& p = S.p;
    printf("enable=0x%03x  zoom=%.3f θ=%.3f pivot=(%.2f,%.2f) trans=(%.3f,%.3f)\n"
           "  chroma=%.4f blur=(%.2f,%.2f,%.2f) γ=%.2f hue=%.4f sat=%.3f\n"
           "  contrast=%.3f decay=%.4f noise=%.4f couple=%.3f ext=%.3f\n",
           S.enable, p.zoom, p.theta, p.pivotX, p.pivotY, p.transX, p.transY,
           p.chroma, p.blurX, p.blurY, p.blurAngle, p.gamma, p.hueRate, p.satGain,
           p.contrast, p.decay, p.noise, p.couple, p.external);
    fflush(stdout);
}

static void toggle_fullscreen() {
    if (!S.win) return;
    if (S.isFullscreen) {
        // Back to windowed at the saved geometry.
        glfwSetWindowMonitor(S.win, nullptr,
                             S.savedX, S.savedY,
                             S.savedW, S.savedH, GLFW_DONT_CARE);
        S.isFullscreen = false;
        printf("[window] windowed %dx%d at (%d,%d)\n",
               S.savedW, S.savedH, S.savedX, S.savedY);
    } else {
        // Save current windowed geometry so we can come back to it.
        int x = 0, y = 0, w = 0, h = 0;
        glfwGetWindowPos(S.win, &x, &y);
        glfwGetWindowSize(S.win, &w, &h);
        S.savedX = x; S.savedY = y; S.savedW = w; S.savedH = h;

        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(S.win, mon, 0, 0,
                             mode->width, mode->height, mode->refreshRate);
        S.isFullscreen = true;
        printf("[window] fullscreen %dx%d @ %dHz\n",
               mode->width, mode->height, mode->refreshRate);
    }
}

// ── presets ──────────────────────────────────────────────────────────────
// Save / load human-editable INI-style files in ./presets/.
// Save state: layer enable bits, quality cycles, all 17 Params, pattern,
// active fields. NOT saved: window size, fullscreen, recording, sim res
// (those are session/hardware concerns, not creative state).

#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

static std::vector<std::string> g_presetFiles;
static int g_currentPreset = -1;

static std::string preset_dir() {
    return g_shader_base.empty() ? "presets" : (g_shader_base + "presets");
}

// Trim whitespace from both ends.
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b-1])) b--;
    return s.substr(a, b - a);
}

static bool parse_bool_onoff(const std::string& v) {
    std::string lo = v;
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
    return lo == "on" || lo == "true" || lo == "1" || lo == "yes";
}

// Save current state to path. Returns true on success.
static bool preset_write(const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    const auto& p = S.p;
    auto io = [&](int bit) { return (S.enable & bit) ? "on" : "off"; };

    time_t t = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char ts[64];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &lt);

    fprintf(f,
"# video feedback preset\n"
"# saved: %s\n"
"# notes: (edit this line to describe the preset; trade with friends)\n"
"\n"
"[layers]\n"
"warp     = %s\n"
"optics   = %s\n"
"gamma    = %s\n"
"color    = %s\n"
"contrast = %s\n"
"decay    = %s\n"
"noise    = %s\n"
"couple   = %s\n"
"external = %s\n"
"inject   = %s\n"
"physics  = %s\n"
"thermal  = %s\n"
"\n"
"[quality]\n"
"# blur:  0=5-tap cross   1=9-tap gauss   2=25-tap gauss\n"
"# ca:    0=3-sample      1=5-sample ramp 2=8-sample wavelen\n"
"# noise: 0=white          1=pink 1/f\n"
"# fields: 1..4 coupled feedback fields\n"
"blur     = %d\n"
"ca       = %d\n"
"noise    = %d\n"
"fields   = %d\n"
"\n"
"[warp]\n"
"zoom     = %.6f\n"
"theta    = %.6f\n"
"pivotX   = %.6f\n"
"pivotY   = %.6f\n"
"transX   = %.6f\n"
"transY   = %.6f\n"
"\n"
"[optics]\n"
"chroma     = %.6f\n"
"blurX      = %.6f\n"
"blurY      = %.6f\n"
"blurAngle  = %.6f\n"
"\n"
"[color]\n"
"gamma    = %.6f\n"
"hueRate  = %.6f\n"
"satGain  = %.6f\n"
"contrast = %.6f\n"
"\n"
"[dynamics]\n"
"decay    = %.6f\n"
"noise    = %.6f\n"
"couple   = %.6f\n"
"external = %.6f\n"
"\n"
"[trigger]\n"
"# pattern: 0=H-bars 1=V-bars 2=dot 3=checker 4=gradient\n"
"pattern  = %d\n"
"\n"
"[physics]\n"
"# Crutchfield 1984 camera-side knobs (see shaders/layers/physics.glsl).\n"
"# invert: 0 or 1 — Crutchfield's s parameter; inverts all channels.\n"
"# sensorGamma: photoconductor response curve, 1.0 = no-op; paper says 0.6..0.9.\n"
"# satKnee: 0 = hard clip; 1 = full Reinhard rolloff.\n"
"# colorCross: 0 = RGB independent; 1 = all channels become mean.\n"
"invert      = %d\n"
"sensorGamma = %.6f\n"
"satKnee     = %.6f\n"
"colorCross  = %.6f\n"
"\n"
"[thermal]\n"
"# Air turbulence between camera and monitor — noise-driven UV displacement.\n"
"# amp: 0=off, 0.01=barely shimmer, 0.05=heat, 0.15+=turbulent.\n"
"# scale: noise spatial frequency (1=wide rolls, 20+=dense).\n"
"# speed: time evolution rate. rise: upward bias (heat). swirl: vorticity.\n"
"amp    = %.6f\n"
"scale  = %.6f\n"
"speed  = %.6f\n"
"rise   = %.6f\n"
"swirl  = %.6f\n",
        ts,
        io(L_WARP), io(L_OPTICS), io(L_GAMMA), io(L_COLOR), io(L_CONTRAST),
        io(L_DECAY), io(L_NOISE), io(L_COUPLE), io(L_EXTERNAL), io(L_INJECT),
        io(L_PHYSICS), io(L_THERMAL),
        S.blurQ, S.caQ, S.noiseQ, S.activeFields,
        p.zoom, p.theta, p.pivotX, p.pivotY, p.transX, p.transY,
        p.chroma, p.blurX, p.blurY, p.blurAngle,
        p.gamma, p.hueRate, p.satGain, p.contrast,
        p.decay, p.noise, p.couple, p.external,
        p.pattern,
        p.invert, p.sensorGamma, p.satKnee, p.colorCross,
        p.thermAmp, p.thermScale, p.thermSpeed, p.thermRise, p.thermSwirl);
    fclose(f);
    return true;
}

// Load file at path, mutating S. Returns true on success.
// Tolerates unknown sections/keys (so old presets keep working when we add new
// keys; new presets just leave default values for keys they don't know about).
static bool preset_load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[1024];
    std::string section;
    auto& p = S.p;

    // Helper: convert layer name to bit
    auto layer_bit = [](const std::string& n) -> int {
        if (n == "warp")     return L_WARP;
        if (n == "optics")   return L_OPTICS;
        if (n == "gamma")    return L_GAMMA;
        if (n == "color")    return L_COLOR;
        if (n == "contrast") return L_CONTRAST;
        if (n == "decay")    return L_DECAY;
        if (n == "noise")    return L_NOISE;
        if (n == "couple")   return L_COUPLE;
        if (n == "external") return L_EXTERNAL;
        if (n == "inject")   return L_INJECT;
        if (n == "physics")  return L_PHYSICS;
        if (n == "thermal")  return L_THERMAL;
        return 0;
    };

    while (fgets(line, sizeof line, f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        if (s.front() == '[' && s.back() == ']') {
            section = s.substr(1, s.size() - 2);
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq));
        std::string v = trim(s.substr(eq + 1));
        // strip trailing inline comment
        size_t hash = v.find('#');
        if (hash != std::string::npos) v = trim(v.substr(0, hash));

        if (section == "layers") {
            int bit = layer_bit(k);
            if (bit) {
                if (parse_bool_onoff(v)) S.enable |=  bit;
                else                     S.enable &= ~bit;
            }
        } else if (section == "quality") {
            int n = atoi(v.c_str());
            if      (k == "blur")   { if (n>=0 && n<=2) S.blurQ = n; }
            else if (k == "ca")     { if (n>=0 && n<=2) S.caQ   = n; }
            else if (k == "noise")  { if (n>=0 && n<=1) S.noiseQ = n; }
            else if (k == "fields") {
                if (n>=1 && n<=4) {
                    S.activeFields = n;
                    // make sure FBOs exist for newly-active fields
                    for (int fi = 0; fi < n; fi++) {
                        if (S.field[fi][0].fbo == 0) {
                            resize_fbo(S.field[fi][0], S.simW, S.simH);
                            resize_fbo(S.field[fi][1], S.simW, S.simH);
                            clear_fbo(S.field[fi][0]);
                            clear_fbo(S.field[fi][1]);
                        }
                    }
                }
            }
        } else if (section == "warp") {
            float fv = (float)atof(v.c_str());
            if      (k == "zoom")   p.zoom   = fv;
            else if (k == "theta")  p.theta  = fv;
            else if (k == "pivotX") p.pivotX = fv;
            else if (k == "pivotY") p.pivotY = fv;
            else if (k == "transX") p.transX = fv;
            else if (k == "transY") p.transY = fv;
        } else if (section == "optics") {
            float fv = (float)atof(v.c_str());
            if      (k == "chroma")    p.chroma    = fv;
            else if (k == "blurX")     p.blurX     = fv;
            else if (k == "blurY")     p.blurY     = fv;
            else if (k == "blurAngle") p.blurAngle = fv;
        } else if (section == "color") {
            float fv = (float)atof(v.c_str());
            if      (k == "gamma")    p.gamma    = fv;
            else if (k == "hueRate")  p.hueRate  = fv;
            else if (k == "satGain")  p.satGain  = fv;
            else if (k == "contrast") p.contrast = fv;
        } else if (section == "dynamics") {
            float fv = (float)atof(v.c_str());
            if      (k == "decay")    p.decay    = fv;
            else if (k == "noise")    p.noise    = fv;
            else if (k == "couple")   p.couple   = fv;
            else if (k == "external") p.external = fv;
        } else if (section == "trigger") {
            int n = atoi(v.c_str());
            if (k == "pattern" && n>=0 && n<=4) p.pattern = n;
        } else if (section == "physics") {
            if (k == "invert") p.invert = (atoi(v.c_str()) ? 1 : 0);
            else {
                float fv = (float)atof(v.c_str());
                if      (k == "sensorGamma") p.sensorGamma = fv;
                else if (k == "satKnee")     p.satKnee     = fv;
                else if (k == "colorCross")  p.colorCross  = fv;
            }
        } else if (section == "thermal") {
            float fv = (float)atof(v.c_str());
            if      (k == "amp")   p.thermAmp   = fv;
            else if (k == "scale") p.thermScale = fv;
            else if (k == "speed") p.thermSpeed = fv;
            else if (k == "rise")  p.thermRise  = fv;
            else if (k == "swirl") p.thermSwirl = fv;
        }
    }
    fclose(f);
    return true;
}

// Scan ./presets/ for *.ini files, sort alphabetically.
static void preset_rescan() {
    g_presetFiles.clear();
    fs::path dir = preset_dir();
    if (!fs::exists(dir)) {
        fs::create_directory(dir);
        return;
    }
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.is_regular_file() && e.path().extension() == ".ini")
            g_presetFiles.push_back(e.path().string());
    }
    std::sort(g_presetFiles.begin(), g_presetFiles.end());
}

// Get the basename (filename minus dir, minus .ini) of currently-selected preset.
static std::string preset_current_name() {
    if (g_currentPreset < 0 || g_currentPreset >= (int)g_presetFiles.size()) return "";
    fs::path p = g_presetFiles[g_currentPreset];
    return p.stem().string();
}

// Cycle to next/prev. Returns new selection name for HUD logging.
static std::string preset_cycle(int dir) {
    if (g_presetFiles.empty()) return "";
    if (g_currentPreset < 0) g_currentPreset = (dir > 0) ? 0 : (int)g_presetFiles.size() - 1;
    else g_currentPreset = (g_currentPreset + dir + (int)g_presetFiles.size())
                           % (int)g_presetFiles.size();
    if (preset_load(g_presetFiles[g_currentPreset])) return preset_current_name();
    return "";
}

// Save current state as presets/auto_YYYYMMDD_HHMMSS.ini, rescan, return path.
static std::string preset_save_now() {
    fs::path dir = preset_dir();
    if (!fs::exists(dir)) fs::create_directory(dir);

    char ts[64];
    time_t t = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    strftime(ts, sizeof ts, "auto_%Y%m%d_%H%M%S.ini", &lt);
    fs::path full = dir / ts;
    if (!preset_write(full.string())) return "";
    preset_rescan();
    // Move current pointer to the file we just saved
    for (size_t i = 0; i < g_presetFiles.size(); i++)
        if (g_presetFiles[i] == full.string()) { g_currentPreset = (int)i; break; }
    return full.filename().string();
}

// Track frame timing for FPS readout in the help overlay.
static double g_lastFrameTime = 0.0;
static double g_smoothedFps   = 60.0;

// Build the in-window help text with current values and grey-out indicators
// for every cycle-able quality. Re-built every frame the help is shown so
// values track live as you turn the knobs with help open.
static std::string build_help_text() {
    const auto& p = S.p;
    auto onoff = [](bool b) { return b ? "[ON] " : " off "; };

    // Render a 2-or-3-option cycle as "[opt0] opt1  opt2" with the current
    // one bracketed and the others shown as muted "alternatives." Since
    // stb_easy_font has no greyed-out style, we use [brackets] and lowercase
    // for the inactive options to convey "available but not picked".
    auto cycle3 = [](int cur, const char* a, const char* b, const char* c) {
        char buf[160];
        snprintf(buf, sizeof buf, "%s%s %s%s %s%s",
            cur==0?"[":" ", a, cur==0?"]":" ",
            cur==1?"[":" ", b, cur==1?"]":" ");
        // append c with bracket logic
        char tail[64];
        snprintf(tail, sizeof tail, " %s%s%s",
                 cur==2?"[":" ", c, cur==2?"]":" ");
        strncat(buf, tail, sizeof(buf) - strlen(buf) - 1);
        return std::string(buf);
    };
    auto cycle2 = [](int cur, const char* a, const char* b) {
        char buf[120];
        snprintf(buf, sizeof buf, "%s%s%s   %s%s%s",
            cur==0?"[":" ", a, cur==0?"]":" ",
            cur==1?"[":" ", b, cur==1?"]":" ");
        return std::string(buf);
    };
    auto cycleN = [](int cur, int n) {
        char buf[80] = "";
        for (int i = 1; i <= n; i++) {
            char piece[16];
            snprintf(piece, sizeof piece, "%s%d%s ",
                     i==cur?"[":" ", i, i==cur?"]":" ");
            strncat(buf, piece, sizeof(buf) - strlen(buf) - 1);
        }
        return std::string(buf);
    };

    static const char* patNames[] = { "H-bars", "V-bars", "dot", "checker", "gradient" };

    // FPS / frame timing
    double now = glfwGetTime();
    double dt  = now - g_lastFrameTime;
    if (dt > 0.0 && dt < 1.0) {
        double inst = 1.0 / dt;
        g_smoothedFps = g_smoothedFps * 0.9 + inst * 0.1;
    }

    char buf[6144];
    snprintf(buf, sizeof buf,
"=== video feedback ===                                       [ press H to close ]\n"
"\n"
"-- STATS --\n"
"sim     : %d x %d   precision: RGBA%dF   vsync: %s\n"
"window  : %d x %d   %s\n"
"fps     : %.1f       iters/frame: %d   recording: %s\n"
"preset  : %s\n"
"\n"
"-- LAYERS (toggle) --\n"
"F1  warp     %s    F6  decay    %s\n"
"F2  optics   %s    F7  noise    %s\n"
"F3  gamma    %s    F8  couple   %s\n"
"F4  color    %s    F9  external %s\n"
"F5  contrast %s    F10 inject   %s\n"
"Ins physics  %s    PgDn thermal %s\n"
"\n"
"-- QUALITY (cycle; current pick in [brackets]) --\n"
"PgUp  blur kernel : %s\n"
"F12   chrom. ab.  : %s\n"
"Home  noise type  : %s\n"
"End   coupled fld : %s\n"
"\n"
"-- ACTIONS --\n"
"F11   toggle fullscreen / windowed\n"
"`     start/stop recording (feedback_<timestamp>.mp4)\n"
"\\     reload shaders from disk (live edit)\n"
"Ctrl+S  save current settings as new preset (./presets/auto_*.ini)\n"
"Ctrl+N  next preset       Ctrl+P  previous preset\n"
"C     clear all fields to black\n"
"P     pause/resume          (currently: %s)\n"
"SPACE inject pattern (hold) (current: %s)\n"
"1..5  pattern: H-bars, V-bars, dot, checker, gradient\n"
"Esc   quit\n"
"\n"
"-- WARP --\n"
"Q/A  zoom         : %.4f /pass   (1.000 = no zoom)\n"
"W/S  rotation     : %+.4f rad/pass = %+.2f deg/sec @60fps\n"
"arr  translate    : (%+.4f, %+.4f) /pass\n"
"\n"
"-- OPTICS --\n"
"[/]  chrom. ab.   : %+.4f\n"
";/'  blur-X       : %.2f px\n"
",/.  blur-Y       : %.2f px\n"
"-/=  blur-angle   : %+.3f rad\n"
"\n"
"-- COLOR / TONE --\n"
"G/B  gamma        : %.3f\n"
"E/D  hue rate     : %+.4f /pass = %+.2f rotations/sec @60fps\n"
"R/F  saturation   : %.3f x\n"
"T/Y  contrast     : %.3f x\n"
"\n"
"-- DYNAMICS --\n"
"U/J  decay        : %.5f /pass\n"
"N/M  noise        : %.4f\n"
"K/I  couple amt   : %.3f\n"
"O/L  external amt : %.3f\n"
"\n"
"-- PHYSICS (Crutchfield 1984 camera-side knobs) --\n"
"V     luminance inv : %s\n"
"Z/X   sensor gamma  : %.3f     (paper says 0.6..0.9; 1.0 = off)\n"
"7/8   sat-knee      : %.3f     (0 = hard clip; 1 = full Reinhard)\n"
"9/0   color-xtalk   : %.3f     (0 = RGB indep; 1 = full averaging)\n"
"\n"
"-- THERMAL (air turbulence between camera & monitor) --\n"
"NP1/4 amplitude  : %.4f    (0=off, 0.015=shimmer, 0.1+=turbulent)\n"
"NP2/5 scale      : %.2f     (1=wide rolls, 20+=fine grain)\n"
"NP3/6 speed      : %.2f     (time evolution rate)\n"
"NP7/8 rise       : %+.3f    (vertical heat advection)\n"
"NP9/0 swirl      : %.3f     (0=translation, 1+=vortical)\n"
"\n"
"Hold SHIFT for 20x coarse steps on any parameter.",
        S.simW, S.simH, g_cfg.precision, g_cfg.vsync ? "on" : "off",
        S.winW, S.winH, S.isFullscreen ? "FULLSCREEN" : "windowed",
        g_smoothedFps, g_cfg.iters,
        S.rec.active()
            ? (S.rec.queueDropped()
                ? "REC (DROPS)"
                : (S.rec.queueDepth() >= 3 ? "REC (slow)" : "REC"))
            : "off",
        g_presetFiles.empty() ? "(none in ./presets/)"
                              : (g_currentPreset < 0 ? "(unsaved)"
                                 : preset_current_name().c_str()),
        onoff(S.enable & L_WARP),     onoff(S.enable & L_DECAY),
        onoff(S.enable & L_OPTICS),   onoff(S.enable & L_NOISE),
        onoff(S.enable & L_GAMMA),    onoff(S.enable & L_COUPLE),
        onoff(S.enable & L_COLOR),    onoff(S.enable & L_EXTERNAL),
        onoff(S.enable & L_CONTRAST), onoff(S.enable & L_INJECT),
        onoff(S.enable & L_PHYSICS), onoff(S.enable & L_THERMAL),
        cycle3(S.blurQ,  "5-tap cross", "9-tap gauss", "25-tap gauss").c_str(),
        cycle3(S.caQ,    "3-sample", "5-sample ramp", "8-sample wavelen").c_str(),
        cycle2(S.noiseQ, "white", "pink 1/f").c_str(),
        cycleN(S.activeFields, 4).c_str(),
        S.paused ? "PAUSED" : "running",
        patNames[(unsigned)p.pattern % 5],
        p.zoom,
        p.theta, p.theta * 57.2958f * 60.0f,
        p.transX, p.transY,
        p.chroma,
        p.blurX,
        p.blurY,
        p.blurAngle,
        p.gamma,
        p.hueRate, p.hueRate * 60.0f,
        p.satGain,
        p.contrast,
        p.decay,
        p.noise,
        p.couple,
        p.external,
        p.invert ? "ON" : "off",
        p.sensorGamma,
        p.satKnee,
        p.colorCross,
        p.thermAmp,
        p.thermScale,
        p.thermSpeed,
        p.thermRise,
        p.thermSwirl);
    g_lastFrameTime = now;
    return std::string(buf);
}

// ── keyboard ──────────────────────────────────────────────────────────────
static GLuint progFeedback = 0, progBlit = 0;

static void reload_shaders() {
    GLuint np = build_feedback_program();
    if (!np) {
        fprintf(stderr, "[shaders] reload failed — keeping previous program\n");
        fflush(stderr);
        return;
    }
    if (progFeedback) glDeleteProgram(progFeedback);
    progFeedback = np;
    printf("[shaders] reloaded\n"); fflush(stdout);
}

static void key_cb(GLFWwindow* w, int key, int, int action, int mods) {
    const bool press  = (action == GLFW_PRESS);
    const bool rept   = (action == GLFW_PRESS || action == GLFW_REPEAT);
    const bool shift  = (mods & GLFW_MOD_SHIFT) != 0;

    // Per-parameter fine step sizes, tuned for 60fps feedback dynamics.
    // Shift multiplies by ~20 for coarse jumps. These are what makes the
    // knobs feel like the trim pots on a physical mixer instead of a blunt
    // instrument — tiny zoom changes produce wildly different cascades, so
    // zoom/theta in particular want very fine control.
    const float m = shift ? 20.0f : 1.0f;
    const float sZoom    = 0.0002f * m;   // 0.02% per pass default
    const float sTheta   = 0.0002f * m;   // ~0.01° per pass default
    const float sTrans   = 0.0005f * m;
    const float sChroma  = 0.0002f * m;
    const float sBlurR   = 0.02f   * m;
    const float sBlurA   = 0.005f  * m;
    const float sGamma   = 0.01f   * m;
    const float sHue     = 0.0002f * m;
    const float sSat     = 0.002f  * m;
    const float sContrast= 0.002f  * m;
    const float sDecay   = 0.0005f * m;
    const float sNoise   = 0.0002f * m;
    const float sCouple  = 0.002f  * m;
    const float sExt     = 0.002f  * m;
    // physics step sizes — matched to each knob's useful range
    const float sSensorG = 0.005f  * m;   // 0.5% per tick; Crutchfield span 0.6..0.9
    const float sKnee    = 0.005f  * m;   // 0.5% per tick; range 0..1
    const float sCross   = 0.002f  * m;   // 0.2% per tick; range 0..1

    // layer toggles
    for (const auto& li : LAYERS)
        if (press && key == li.fkey) {
            S.enable ^= li.bit;
            bool on = (S.enable & li.bit) != 0;
            printf("[%s] %s\n", li.name, on ? "ON" : "off");
            char buf[64]; snprintf(buf, sizeof buf, "%s: %s", li.name, on ? "ON" : "off");
            S.ov.logEvent(buf);
            return;
        }

    if (press) switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); return;
        case GLFW_KEY_P:
            if (mods & GLFW_MOD_CONTROL) {
                std::string n = preset_cycle(-1);
                if (!n.empty()) {
                    printf("[preset] loaded %s  (%d/%zu)\n", n.c_str(),
                           g_currentPreset+1, g_presetFiles.size());
                    char b[128]; snprintf(b, sizeof b, "preset: %s  (%d/%zu)",
                                          n.c_str(), g_currentPreset+1, g_presetFiles.size());
                    S.ov.logEvent(b);
                } else {
                    S.ov.logEvent("no presets in ./presets/");
                }
            } else {
                S.paused = !S.paused;
                printf("[%s]\n", S.paused ? "paused" : "running");
                S.ov.logEvent(S.paused ? "paused" : "running");
            }
            return;
        case GLFW_KEY_C: S.needClear = true; printf("[cleared]\n");
                         S.ov.logEvent("cleared"); return;
        case GLFW_KEY_BACKSLASH: reload_shaders();
                                 S.ov.logEvent("shaders reloaded"); return;
        case GLFW_KEY_H: S.ov.toggleHelp();
                         printf("[help] %s\n", S.ov.helpVisible() ? "shown" : "hidden"); return;
        case GLFW_KEY_SLASH: print_help(); return;   // '?' key (shifted /)
        case GLFW_KEY_SPACE: {
            S.p.inject = 1.0f;
            static const char* names[] = {"H-bars","V-bars","dot","checker","gradient"};
            printf("[inject pattern=%d]\n", S.p.pattern);
            char buf[64]; snprintf(buf, sizeof buf, "inject: %s", names[S.p.pattern]);
            S.ov.logEvent(buf); return;
        }
        case GLFW_KEY_1: S.p.pattern = 0; printf("[pattern] H-bars\n");
                         S.ov.logEvent("pattern: H-bars"); return;
        case GLFW_KEY_2: S.p.pattern = 1; printf("[pattern] V-bars\n");
                         S.ov.logEvent("pattern: V-bars"); return;
        case GLFW_KEY_3: S.p.pattern = 2; printf("[pattern] dot\n");
                         S.ov.logEvent("pattern: dot"); return;
        case GLFW_KEY_4: S.p.pattern = 3; printf("[pattern] checker\n");
                         S.ov.logEvent("pattern: checker"); return;
        case GLFW_KEY_5: S.p.pattern = 4; printf("[pattern] gradient\n");
                         S.ov.logEvent("pattern: gradient"); return;
        case GLFW_KEY_V: {
            S.p.invert = 1 - S.p.invert;
            printf("[invert] %s\n", S.p.invert ? "ON" : "off");
            char b[64]; snprintf(b, sizeof b, "luminance invert: %s",
                                 S.p.invert ? "ON" : "off");
            S.ov.logEvent(b); return;
        }
        case GLFW_KEY_GRAVE_ACCENT:
            if (S.rec.active()) {
                S.rec.stop();
                if (!S.rec.lastDir().empty())
                    S.recordingsThisSession.push_back(S.rec.lastDir());
                S.ov.logEvent("recording: stopped");
            }
            else                { Recorder::Config rcfg{};
                                  rcfg.ramBudgetMB   = g_cfg.recRamMB;
                                  rcfg.encoderThreads = g_cfg.recEncoders;
                                  rcfg.uncompressed  = g_cfg.recUncompressed;
                                  S.rec.start(S.simW, S.simH,
                                              g_cfg.recFps, g_cfg.precision,
                                              S.win, rcfg);
                                  S.ov.logEvent("recording: started"); }
            return;

        // ── presets (Ctrl modifier; Ctrl+S save, Ctrl+N next, Ctrl+P prev) ──
        case GLFW_KEY_S:
            if (mods & GLFW_MOD_CONTROL) {
                std::string fn = preset_save_now();
                if (!fn.empty()) {
                    printf("[preset] saved %s\n", fn.c_str());
                    char b[128]; snprintf(b, sizeof b, "preset SAVED: %s", fn.c_str());
                    S.ov.logEvent(b);
                } else {
                    S.ov.logEvent("preset save FAILED");
                }
                return;
            }
            // No Ctrl: don't consume; fall through so the param-section S
            // still works for rotation-down.
            break;
        case GLFW_KEY_N:
            if (mods & GLFW_MOD_CONTROL) {
                std::string n = preset_cycle(+1);
                if (!n.empty()) {
                    printf("[preset] loaded %s  (%d/%zu)\n", n.c_str(),
                           g_currentPreset+1, g_presetFiles.size());
                    char b[128]; snprintf(b, sizeof b, "preset: %s  (%d/%zu)",
                                          n.c_str(), g_currentPreset+1, g_presetFiles.size());
                    S.ov.logEvent(b);
                } else {
                    S.ov.logEvent("no presets in ./presets/");
                }
                return;
            }
            // No Ctrl: fall through to N=noise+ in param section
            break;

        // ── quality cycles (live, no restart) ──
        case GLFW_KEY_F11: {
            toggle_fullscreen();
            char b[64]; snprintf(b, sizeof b, "fullscreen: %s",
                                 S.isFullscreen ? "ON" : "off");
            S.ov.logEvent(b); return;
        }
        case GLFW_KEY_PAGE_UP: {
            S.blurQ = (S.blurQ + 1) % 3;
            const char* n[] = {"5-tap cross","9-tap gauss","25-tap gauss"};
            printf("[blur-q] %s\n", n[S.blurQ]);
            char b[64]; snprintf(b, sizeof b, "blur: %s", n[S.blurQ]);
            S.ov.logEvent(b); return;
        }
        case GLFW_KEY_F12: {
            S.caQ = (S.caQ + 1) % 3;
            const char* n[] = {"3-sample","5-ramp","8-wavelen"};
            printf("[ca-q] %s\n", n[S.caQ]);
            char b[64]; snprintf(b, sizeof b, "CA: %s", n[S.caQ]);
            S.ov.logEvent(b); return;
        }
        case GLFW_KEY_HOME: {
            S.noiseQ = (S.noiseQ + 1) % 2;
            const char* n[] = {"white","pink 1/f"};
            printf("[noise-q] %s\n", n[S.noiseQ]);
            char b[64]; snprintf(b, sizeof b, "noise: %s", n[S.noiseQ]);
            S.ov.logEvent(b); return;
        }
        case GLFW_KEY_END: {
            // Cycle 1 → 2 → 3 → 4 → 1. Allocating a new field's FBOs on demand.
            S.activeFields = (S.activeFields % 4) + 1;
            for (int f = 0; f < S.activeFields; f++) {
                if (S.field[f][0].fbo == 0) {
                    resize_fbo(S.field[f][0], S.simW, S.simH);
                    resize_fbo(S.field[f][1], S.simW, S.simH);
                    clear_fbo(S.field[f][0]);
                    clear_fbo(S.field[f][1]);
                }
            }
            printf("[fields] %d\n", S.activeFields);
            char b[64]; snprintf(b, sizeof b, "fields: %d", S.activeFields);
            S.ov.logEvent(b); return;
        }
    }
    if (action == GLFW_RELEASE && key == GLFW_KEY_SPACE) S.p.inject = 0.3f;

    if (rept) {
        auto& p = S.p;

        // Helper: format a parameter change and post to the overlay.
        // scaleDelta/scaleValue convert from raw units to display units, and
        // 'fmt' is the printf format (e.g. "%+.3f"). Keys are the same as the
        // key press so repeated presses aggregate into one HUD line.
        auto post = [&](const char* key, const char* label,
                        float scaleDelta, const char* unitDelta,
                        float curRaw, float scaleValue, const char* unitValue,
                        const char* fmt) {
            static std::map<std::string, float> cumByKey;
            static std::map<std::string, double> lastHit;
            double now = glfwGetTime();
            // Reset cumulative if this key has been quiet past fade-out
            if (now - lastHit[key] > 6.0) cumByKey[key] = 0;
            lastHit[key] = now;
            // We don't know the per-press delta here — caller tells us via scaleDelta
            // Actually, simplest: use the VALUE change as the delta approximation is
            // not right. Instead caller passes delta directly via a wrapper.
            (void)scaleDelta; (void)unitDelta; (void)curRaw; (void)scaleValue;
            (void)unitValue; (void)fmt; (void)label;
        };
        (void)post;  // placeholder — using the hud() helper below instead.

        // hud(): post one param change to the overlay with cumulative delta accumulation.
        auto hud = [&](const char* key, const char* label,
                       float rawDelta, float rawValue,
                       float scaleD, const char* unitD,
                       float scaleV, const char* unitV)
        {
            // Static per-process state keyed by `key`.
            static std::map<std::string, float>  cum;
            static std::map<std::string, double> lastT;
            double now = glfwGetTime();
            if (now - lastT[key] > 6.0) cum[key] = 0;
            cum[key] += rawDelta;
            lastT[key] = now;
            char d[64], v[64];
            snprintf(d, sizeof d, "%+.3f %s", cum[key] * scaleD, unitD);
            snprintf(v, sizeof v, "%+.3f %s", rawValue * scaleV, unitV);
            S.ov.logParam(key, label, d, v);
        };

        switch (key) {
            // warp
            case GLFW_KEY_Q: p.zoom  += sZoom;
                hud("zoom","zoom", sZoom, p.zoom-1.0f, 100.f,"% /pass", 100.f,"% /pass"); break;
            case GLFW_KEY_A: p.zoom  -= sZoom;
                hud("zoom","zoom",-sZoom, p.zoom-1.0f, 100.f,"% /pass", 100.f,"% /pass"); break;
            case GLFW_KEY_W: p.theta += sTheta;
                hud("theta","rotation", sTheta, p.theta, 57.2958f,"deg/pass", 57.2958f*60.f,"deg/s"); break;
            case GLFW_KEY_S: p.theta -= sTheta;
                hud("theta","rotation",-sTheta, p.theta, 57.2958f,"deg/pass", 57.2958f*60.f,"deg/s"); break;
            case GLFW_KEY_LEFT:  p.transX -= sTrans;
                hud("trX","trans-X",-sTrans, p.transX, 100.f,"% /pass", 100.f,"% of frame"); break;
            case GLFW_KEY_RIGHT: p.transX += sTrans;
                hud("trX","trans-X", sTrans, p.transX, 100.f,"% /pass", 100.f,"% of frame"); break;
            case GLFW_KEY_UP:    p.transY -= sTrans;
                hud("trY","trans-Y",-sTrans, p.transY, 100.f,"% /pass", 100.f,"% of frame"); break;
            case GLFW_KEY_DOWN:  p.transY += sTrans;
                hud("trY","trans-Y", sTrans, p.transY, 100.f,"% /pass", 100.f,"% of frame"); break;
            // optics
            case GLFW_KEY_LEFT_BRACKET:  p.chroma -= sChroma;
                hud("chr","chroma",-sChroma, p.chroma, 1000.f,"milli", 1000.f,"milli"); break;
            case GLFW_KEY_RIGHT_BRACKET: p.chroma += sChroma;
                hud("chr","chroma", sChroma, p.chroma, 1000.f,"milli", 1000.f,"milli"); break;
            case GLFW_KEY_SEMICOLON:     p.blurX  -= sBlurR;
                hud("blrX","blur-X",-sBlurR, p.blurX, 1.f,"px", 1.f,"px"); break;
            case GLFW_KEY_APOSTROPHE:    p.blurX  += sBlurR;
                hud("blrX","blur-X", sBlurR, p.blurX, 1.f,"px", 1.f,"px"); break;
            case GLFW_KEY_COMMA:         p.blurY  -= sBlurR;
                hud("blrY","blur-Y",-sBlurR, p.blurY, 1.f,"px", 1.f,"px"); break;
            case GLFW_KEY_PERIOD:        p.blurY  += sBlurR;
                hud("blrY","blur-Y", sBlurR, p.blurY, 1.f,"px", 1.f,"px"); break;
            case GLFW_KEY_MINUS:         p.blurAngle -= sBlurA;
                hud("blrA","blur-ang",-sBlurA, p.blurAngle, 57.2958f,"deg", 57.2958f,"deg"); break;
            case GLFW_KEY_EQUAL:         p.blurAngle += sBlurA;
                hud("blrA","blur-ang", sBlurA, p.blurAngle, 57.2958f,"deg", 57.2958f,"deg"); break;
            // gamma
            case GLFW_KEY_G: p.gamma += sGamma;
                hud("gam","gamma", sGamma, p.gamma, 1.f,"", 1.f,""); break;
            case GLFW_KEY_B: p.gamma  = fmaxf(0.1f, p.gamma - sGamma);
                hud("gam","gamma",-sGamma, p.gamma, 1.f,"", 1.f,""); break;
            // color
            case GLFW_KEY_E: p.hueRate += sHue;
                hud("hue","hue rate", sHue, p.hueRate, 1000.f,"milli/pass", 60.f,"rot/s"); break;
            case GLFW_KEY_D: p.hueRate -= sHue;
                hud("hue","hue rate",-sHue, p.hueRate, 1000.f,"milli/pass", 60.f,"rot/s"); break;
            case GLFW_KEY_R: p.satGain += sSat;
                hud("sat","satur", sSat, p.satGain, 100.f,"%", 1.f,"x"); break;
            case GLFW_KEY_F: p.satGain -= sSat;
                hud("sat","satur",-sSat, p.satGain, 100.f,"%", 1.f,"x"); break;
            // contrast
            case GLFW_KEY_T: p.contrast += sContrast;
                hud("con","contrast", sContrast, p.contrast, 100.f,"%", 1.f,"x"); break;
            case GLFW_KEY_Y: p.contrast -= sContrast;
                hud("con","contrast",-sContrast, p.contrast, 100.f,"%", 1.f,"x"); break;
            // decay
            case GLFW_KEY_U: p.decay = fminf(1.0f, p.decay + sDecay);
                hud("dec","decay", sDecay, p.decay, 1000.f,"milli", 1000.f,"milli"); break;
            case GLFW_KEY_J: p.decay = fmaxf(0.9f, p.decay - sDecay);
                hud("dec","decay",-sDecay, p.decay, 1000.f,"milli", 1000.f,"milli"); break;
            // noise
            case GLFW_KEY_N: p.noise += sNoise;
                hud("noi","noise", sNoise, p.noise, 1000.f,"milli", 1000.f,"milli"); break;
            case GLFW_KEY_M: p.noise = fmaxf(0.0f, p.noise - sNoise);
                hud("noi","noise",-sNoise, p.noise, 1000.f,"milli", 1000.f,"milli"); break;
            // couple / external
            case GLFW_KEY_K: p.couple   = fminf(1.0f, p.couple   + sCouple);
                hud("cpl","couple", sCouple, p.couple, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_I: p.couple   = fmaxf(0.0f, p.couple   - sCouple);
                hud("cpl","couple",-sCouple, p.couple, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_O: p.external = fminf(1.0f, p.external + sExt);
                hud("ext","external", sExt, p.external, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_L: p.external = fmaxf(0.0f, p.external - sExt);
                hud("ext","external",-sExt, p.external, 100.f,"%", 100.f,"%"); break;
            // physics (Crutchfield camera-side knobs)
            case GLFW_KEY_X: p.sensorGamma = fminf(2.0f, p.sensorGamma + sSensorG);
                hud("sgam","sens-gamma", sSensorG, p.sensorGamma, 1.f,"", 1.f,""); break;
            case GLFW_KEY_Z: p.sensorGamma = fmaxf(0.1f, p.sensorGamma - sSensorG);
                hud("sgam","sens-gamma",-sSensorG, p.sensorGamma, 1.f,"", 1.f,""); break;
            case GLFW_KEY_8: p.satKnee = fminf(1.0f, p.satKnee + sKnee);
                hud("knee","sat-knee", sKnee, p.satKnee, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_7: p.satKnee = fmaxf(0.0f, p.satKnee - sKnee);
                hud("knee","sat-knee",-sKnee, p.satKnee, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_0: p.colorCross = fminf(1.0f, p.colorCross + sCross);
                hud("cx","color-xtalk", sCross, p.colorCross, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_9: p.colorCross = fmaxf(0.0f, p.colorCross - sCross);
                hud("cx","color-xtalk",-sCross, p.colorCross, 100.f,"%", 100.f,"%"); break;
            // thermal (numpad column layout: col1=amp, col2=scale, col3=speed;
            // NP7/8=rise, NP9/NP0-Minus=swirl. Shift = 20x coarse.)
            case GLFW_KEY_KP_4: p.thermAmp = fminf(1.0f, p.thermAmp + 0.002f * m);
                hud("tAmp","therm-amp",  0.002f*m, p.thermAmp, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_KP_1: p.thermAmp = fmaxf(0.0f, p.thermAmp - 0.002f * m);
                hud("tAmp","therm-amp", -0.002f*m, p.thermAmp, 100.f,"%", 100.f,"%"); break;
            case GLFW_KEY_KP_5: p.thermScale = fminf(40.0f, p.thermScale + 0.2f * m);
                hud("tSca","therm-scale", 0.2f*m, p.thermScale, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_2: p.thermScale = fmaxf(0.2f, p.thermScale - 0.2f * m);
                hud("tSca","therm-scale",-0.2f*m, p.thermScale, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_6: p.thermSpeed = fminf(30.0f, p.thermSpeed + 0.1f * m);
                hud("tSpd","therm-speed", 0.1f*m, p.thermSpeed, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_3: p.thermSpeed = fmaxf(0.0f, p.thermSpeed - 0.1f * m);
                hud("tSpd","therm-speed",-0.1f*m, p.thermSpeed, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_8: p.thermRise = fminf(2.0f, p.thermRise + 0.02f * m);
                hud("tRis","therm-rise", 0.02f*m, p.thermRise, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_7: p.thermRise = fmaxf(-1.0f, p.thermRise - 0.02f * m);
                hud("tRis","therm-rise",-0.02f*m, p.thermRise, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_9: p.thermSwirl = fmaxf(0.0f, p.thermSwirl - 0.02f * m);
                hud("tSwi","therm-swirl",-0.02f*m, p.thermSwirl, 1.f,"", 1.f,""); break;
            case GLFW_KEY_KP_0: p.thermSwirl = fminf(2.0f, p.thermSwirl + 0.02f * m);
                hud("tSwi","therm-swirl", 0.02f*m, p.thermSwirl, 1.f,"", 1.f,""); break;
            default: return;
        }
        print_status();
    }
}

static void size_cb(GLFWwindow*, int w, int h) {
    // EXR sequence recording is FBO-based (sim resolution) — window resize
    // is unrelated, so we don't need to stop recording.
    S.winW = w; S.winH = h;
    S.ov.resize(w, h);
}

// ── one feedback step for a single field ──────────────────────────────────
static void render_field(int fieldId, FBO& src, FBO& dst, FBO& otherSrc) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, dst.w, dst.h);
    glUseProgram(progFeedback);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, otherSrc.tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, S.camTex ? S.camTex : src.tex);

    #define U1f(n, v)  glUniform1f (glGetUniformLocation(progFeedback, n), (v))
    #define U1i(n, v)  glUniform1i (glGetUniformLocation(progFeedback, n), (v))
    #define U1ui(n, v) glUniform1ui(glGetUniformLocation(progFeedback, n), (v))
    #define U2f(n, x, y) glUniform2f(glGetUniformLocation(progFeedback, n), (x), (y))

    U1i("uPrev", 0); U1i("uOther", 1); U1i("uCam", 2);
    U2f("uRes", (float)dst.w, (float)dst.h);
    U1f("uTime", (float)glfwGetTime());
    U1ui("uFrame", S.frame);
    U1i("uEnable", S.enable);
    U1i("uFieldId", fieldId);

    auto& p = S.p;
    U1f("uZoom", p.zoom); U1f("uTheta", p.theta);
    U1f("uPivotX", p.pivotX); U1f("uPivotY", p.pivotY);
    U1f("uTransX", p.transX); U1f("uTransY", p.transY);
    U1f("uChroma", p.chroma);
    U1f("uBlurX", p.blurX); U1f("uBlurY", p.blurY); U1f("uBlurAngle", p.blurAngle);
    U1i("uBlurQuality", S.blurQ);
    U1i("uCAQuality",   S.caQ);
    U1f("uGamma", p.gamma);
    U1f("uHueRate", p.hueRate); U1f("uSatGain", p.satGain);
    U1f("uContrast", p.contrast);
    U1f("uDecay", p.decay);
    U1f("uNoise", p.noise);
    U1i("uNoiseQuality", S.noiseQ);
    U1i("uInvert",      p.invert);
    U1f("uSensorGamma", p.sensorGamma);
    U1f("uSatKnee",     p.satKnee);
    U1f("uColorCross",  p.colorCross);
    U1f("uThermAmp",    p.thermAmp);
    U1f("uThermScale",  p.thermScale);
    U1f("uThermSpeed",  p.thermSpeed);
    U1f("uThermRise",   p.thermRise);
    U1f("uThermSwirl",  p.thermSwirl);
    U1f("uCouple", p.couple);
    U1f("uExternal", p.external);
    U1f("uInject", p.inject);
    U1i("uPattern", p.pattern);

    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ── post-session encode prompt ──────────────────────────────────────────
// On exit, offers to convert each EXR sequence to MP4 via ffmpeg. Shows
// disk usage and rough output-size estimates for various quality presets.

namespace {

enum class PresetKind { Normal, All, Skip };

struct EncodePreset {
    const char* name;       // shown to user
    const char* desc;       // brief description
    int  scaleW;            // 0 = no rescale (use source resolution)
    int  scaleH;
    int  quality;           // CRF (CPU) or CQ (NVENC); lower = better. 0 = n/a.
    int  speedTier;         // 0=fast, 1=balanced, 2=slow, 3=slowest/best
    bool hevc;              // true = H.265/HEVC, false = H.264
    // Rough bits-per-pixel estimate for video-feedback content at this quality.
    // Used only for output size hints — actual sizes will vary.
    float bpp;
    PresetKind kind;
};

// Speed-tier → encoder-specific preset name.
//   NVENC: p1 (fastest) .. p7 (slowest/best).
//   libx264/libx265: fast/medium/slow/slower.
static const char* NVENC_PRESETS[4] = { "p4", "p5", "p6", "p7" };
static const char* X264_PRESETS[4]  = { "fast", "medium", "slow", "slower" };
static const char* X265_PRESETS[4]  = { "fast", "medium", "slow", "slower" };

// Presets are ordered so pressing ENTER uses the recommended default (#1):
// HEVC at source resolution, high quality. HEVC is ~30–40% smaller than H.264
// at equivalent perceptual quality; hevc_nvenc handles it trivially on any
// Maxwell-2nd-gen+ NVIDIA GPU, and libx265 is the CPU fallback.
static const EncodePreset PRESETS[] = {
    { "1", "MP4 H.265, source resolution, high quality (recommended)",
      0, 0, 22, 3, true,  0.18f, PresetKind::Normal },
    { "2", "MP4 H.265, source resolution, balanced",
      0, 0, 26, 2, true,  0.10f, PresetKind::Normal },
    { "3", "MP4 H.265, 1080p, small file",
      1920, 1080, 30, 1, true,  0.06f, PresetKind::Normal },
    { "4", "MP4 H.264, source resolution, high quality (max compatibility)",
      0, 0, 19, 3, false, 0.30f, PresetKind::Normal },
    { "5", "MP4 H.264, 1080p, balanced (max compatibility)",
      1920, 1080, 23, 2, false, 0.15f, PresetKind::Normal },
    { "6", "ALL — encode every preset above with distinct filenames (DEBUG, slow)",
      0, 0, 0, 0, false, 0.0f, PresetKind::All },
    { "7", "Keep EXR sequence only, do not encode",
      0, 0, 0, 0, false, 0.0f, PresetKind::Skip },
};
constexpr int N_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// Filename suffix identifying codec family, resolution, quality target, and
// speed tier. Stable and sortable so ALL-mode outputs line up side by side.
static std::string preset_suffix(const EncodePreset& p) {
    char buf[64];
    const char* codec = p.hevc ? "hevc" : "h264";
    if (p.scaleW > 0) {
        std::snprintf(buf, sizeof buf, "__%s_%dp_q%d_s%d",
                      codec, p.scaleH, p.quality, p.speedTier);
    } else {
        std::snprintf(buf, sizeof buf, "__%s_src_q%d_s%d",
                      codec, p.quality, p.speedTier);
    }
    return buf;
}

static uint64_t dir_size_bytes(const std::string& path) {
    uint64_t total = 0;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(path, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec)) total += e.file_size(ec);
    }
    return total;
}

static int dir_frame_count(const std::string& path) {
    int n = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) &&
            e.path().extension() == ".exr") n++;
    }
    return n;
}

static std::string human_bytes(uint64_t b) {
    char buf[64];
    if (b > 1024ull*1024*1024) std::snprintf(buf, sizeof buf, "%.2f GB",
        (double)b / (1024.0*1024.0*1024.0));
    else if (b > 1024*1024) std::snprintf(buf, sizeof buf, "%.1f MB",
        (double)b / (1024.0*1024.0));
    else if (b > 1024) std::snprintf(buf, sizeof buf, "%.1f KB",
        (double)b / 1024.0);
    else std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    return buf;
}

// Read manifest.txt to recover sim resolution, fps, and drop stats.
// framesDropped defaults to 0 for manifests that predate that field.
static bool read_manifest(const std::string& dir, int& w, int& h, int& fps,
                          int& framesDropped) {
    w = h = fps = 0;
    framesDropped = 0;
    FILE* f = std::fopen((fs::path(dir) / "manifest.txt").string().c_str(), "r");
    if (!f) return false;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "resolution", 10) == 0) {
            const char* eq = std::strchr(line, '=');
            if (eq) std::sscanf(eq + 1, " %dx%d", &w, &h);
        } else if (std::strncmp(line, "fps", 3) == 0) {
            const char* eq = std::strchr(line, '=');
            if (eq) std::sscanf(eq + 1, " %d", &fps);
        } else if (std::strncmp(line, "frames_dropped", 14) == 0) {
            const char* eq = std::strchr(line, '=');
            if (eq) std::sscanf(eq + 1, " %d", &framesDropped);
        }
    }
    std::fclose(f);
    return (w > 0 && h > 0 && fps > 0);
}

// Estimate output size: bpp × pixels × frames / 8.
static uint64_t estimate_output_bytes(const EncodePreset& p, int w, int h, int frames) {
    int outW = (p.scaleW > 0) ? p.scaleW : w;
    int outH = (p.scaleH > 0) ? p.scaleH : h;
    return (uint64_t)((double)outW * outH * frames * p.bpp / 8.0);
}

enum class Encoder { NVENC_HEVC, NVENC_H264, X265, X264 };

// Probe whether an encoder can actually init on this machine — lists the
// encoder only proves it was compiled in, not that the driver is loaded, so
// we run a ~0.1s real encode against a synthetic source. Cached per-encoder.
static bool ffmpeg_encoder_works(const char* enc) {
    char cmd[256];
    std::snprintf(cmd, sizeof cmd,
        "ffmpeg -hide_banner -loglevel quiet "
        "-f lavfi -i nullsrc=s=256x256:d=0.1 -c:v %s -f null NUL >nul 2>&1",
        enc);
    return std::system(cmd) == 0;
}

static bool nvenc_hevc_available() {
    static int cached = -1;
    if (cached == -1) cached = ffmpeg_encoder_works("hevc_nvenc") ? 1 : 0;
    return cached == 1;
}
static bool nvenc_h264_available() {
    static int cached = -1;
    if (cached == -1) cached = ffmpeg_encoder_works("h264_nvenc") ? 1 : 0;
    return cached == 1;
}

static Encoder pick_encoder(const EncodePreset& p) {
    if (p.hevc) return nvenc_hevc_available() ? Encoder::NVENC_HEVC : Encoder::X265;
    return           nvenc_h264_available() ? Encoder::NVENC_H264 : Encoder::X264;
}

static const char* encoder_label(Encoder e) {
    switch (e) {
        case Encoder::NVENC_HEVC: return "hevc_nvenc (GPU)";
        case Encoder::NVENC_H264: return "h264_nvenc (GPU)";
        case Encoder::X265:       return "libx265 (CPU)";
        case Encoder::X264:       return "libx264 (CPU)";
    }
    return "?";
}

static void run_ffmpeg_encode(const std::string& dir, const EncodePreset& p,
                              int srcW, int srcH, double fps,
                              const std::string& filenameSuffix = "") {
    (void)srcW; (void)srcH;
    Encoder enc = pick_encoder(p);
    std::string outPath = dir + filenameSuffix + ".mp4";

    // Encoder-specific flags.
    //   NVENC: VBR with cq target (CRF-equivalent), lookahead + AQ for quality,
    //          B-frames as reference (Turing+, safe on all Ampere).
    //   HEVC in MP4 gets tagged hvc1 for QuickTime/iOS/macOS playback.
    char encArgs[512];
    switch (enc) {
        case Encoder::NVENC_HEVC:
            std::snprintf(encArgs, sizeof encArgs,
                "-c:v hevc_nvenc -preset %s -tune hq -rc vbr -cq %d -b:v 0 "
                "-spatial-aq 1 -temporal-aq 1 -rc-lookahead 32 "
                "-bf 3 -b_ref_mode middle -tag:v hvc1",
                NVENC_PRESETS[p.speedTier], p.quality);
            break;
        case Encoder::NVENC_H264:
            std::snprintf(encArgs, sizeof encArgs,
                "-c:v h264_nvenc -preset %s -tune hq -rc vbr -cq %d -b:v 0 "
                "-spatial-aq 1 -temporal-aq 1 -rc-lookahead 32 "
                "-bf 3 -b_ref_mode middle",
                NVENC_PRESETS[p.speedTier], p.quality);
            break;
        case Encoder::X265:
            std::snprintf(encArgs, sizeof encArgs,
                "-c:v libx265 -preset %s -crf %d -tag:v hvc1",
                X265_PRESETS[p.speedTier], p.quality);
            break;
        case Encoder::X264:
            std::snprintf(encArgs, sizeof encArgs,
                "-c:v libx264 -preset %s -crf %d",
                X264_PRESETS[p.speedTier], p.quality);
            break;
    }

    char vfArg[128] = "";
    if (p.scaleW > 0) {
        std::snprintf(vfArg, sizeof vfArg, "-vf scale=%d:%d:flags=lanczos ",
                      p.scaleW, p.scaleH);
    }

    char cmd[2048];
    std::snprintf(cmd, sizeof cmd,
        "ffmpeg -y -framerate %g -i \"%s/frame_%%06d.exr\" "
        "%s %s"
        "-pix_fmt yuv420p \"%s\"",
        fps, dir.c_str(), encArgs, vfArg, outPath.c_str());

    std::printf("\n  encoding: %s\n  encoder : %s\n  command : %s\n\n",
                outPath.c_str(), encoder_label(enc), cmd);
    std::fflush(stdout);
    int rc = std::system(cmd);
    if (rc == 0) {
        uint64_t actual = 0;
        std::error_code ec;
        actual = fs::file_size(outPath, ec);
        std::printf("  done — %s (%s)\n", outPath.c_str(), human_bytes(actual).c_str());
    } else {
        std::printf("  ffmpeg returned non-zero (%d) — check output above\n", rc);
    }
}

static void encode_prompt(const std::vector<std::string>& dirs) {
    if (dirs.empty()) return;

    std::printf("\n");
    std::printf("════════════════════════════════════════════════════════════\n");
    std::printf("  Recordings captured this session\n");
    std::printf("════════════════════════════════════════════════════════════\n");

    struct Entry {
        std::string dir;
        int w, h, fps, frames, dropped;
        uint64_t bytes;
        double realFps;   // effective capture rate (= fps if no drops)
    };
    std::vector<Entry> entries;
    for (auto& d : dirs) {
        if (!fs::exists(d)) continue;
        Entry e;
        e.dir = d;
        if (!read_manifest(d, e.w, e.h, e.fps, e.dropped)) continue;
        e.frames = dir_frame_count(d);
        e.bytes = dir_size_bytes(d);
        // If the recorder dropped frames (disk couldn't keep up), the file
        // manifest's nominal fps will play back too fast. Scale to the
        // effective capture rate so playback matches wall-clock.
        int total = e.frames + e.dropped;
        e.realFps = (e.dropped > 0 && total > 0)
            ? (double)e.fps * (double)e.frames / (double)total
            : (double)e.fps;
        entries.push_back(e);
    }
    if (entries.empty()) {
        std::printf("  (none with valid manifests)\n");
        return;
    }

    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        double dur = (e.realFps > 0) ? (double)e.frames / e.realFps : 0.0;
        std::printf("\n  [%zu] %s\n", i + 1, e.dir.c_str());
        std::printf("      %dx%d @ %d fps,  %d frames (~%.1f sec),  %s on disk\n",
                    e.w, e.h, e.fps, e.frames, dur, human_bytes(e.bytes).c_str());
        if (e.dropped > 0) {
            double dropPct = 100.0 * (double)e.dropped / (double)(e.frames + e.dropped);
            std::printf("      WARNING: %d dropped frame(s) (%.1f%%). Encoding at real %.2f fps "
                        "so playback matches wall-clock.\n",
                        e.dropped, dropPct, e.realFps);
        }
    }

    std::printf("\n");
    std::printf("Encoding presets:\n");
    for (int i = 0; i < N_PRESETS; i++) {
        std::printf("  [%s] %s\n", PRESETS[i].name, PRESETS[i].desc);
    }

    std::printf("\nFor each recording, choose a preset (or %d to skip).\n", N_PRESETS);
    std::printf("Hit ENTER on a blank line to use the default / last answer.\n\n");

    int sticky = 1;  // default = recommended (HEVC, source res, high quality)
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        std::printf("  [%zu/%zu] %s   ", i + 1, entries.size(),
                    fs::path(e.dir).filename().string().c_str());

        // Per-preset size estimates. For ALL, show sum across every normal
        // preset; for Skip, show nothing.
        uint64_t allSum = 0;
        for (int k = 0; k < N_PRESETS; k++) {
            const auto& pk = PRESETS[k];
            if (pk.kind == PresetKind::Normal) {
                uint64_t est = estimate_output_bytes(pk, e.w, e.h, e.frames);
                allSum += est;
                std::printf("[%s]≈%s  ", pk.name, human_bytes(est).c_str());
            } else if (pk.kind == PresetKind::All) {
                std::printf("[%s]≈%s  ", pk.name, human_bytes(allSum).c_str());
            }
        }
        std::printf("\n  choose [1-%d] (ENTER = %d): ", N_PRESETS, sticky);
        std::fflush(stdout);

        char input[16];
        if (!std::fgets(input, sizeof input, stdin)) break;
        int pick = 0;
        if (input[0] == '\n' || input[0] == '\0') pick = sticky;
        else pick = std::atoi(input);
        if (pick < 1 || pick > N_PRESETS) {
            std::printf("  (invalid choice; skipping)\n");
            continue;
        }
        sticky = pick;
        const EncodePreset& p = PRESETS[pick - 1];
        if (p.kind == PresetKind::Skip) continue;

        if (p.kind == PresetKind::All) {
            std::printf("  ALL mode: encoding %s through every preset "
                        "(this will take a while)\n", e.dir.c_str());
            for (int k = 0; k < N_PRESETS; k++) {
                const auto& pk = PRESETS[k];
                if (pk.kind != PresetKind::Normal) continue;
                run_ffmpeg_encode(e.dir, pk, e.w, e.h, e.realFps, preset_suffix(pk));
            }
            continue;
        }

        run_ffmpeg_encode(e.dir, p, e.w, e.h, e.realFps);
    }

    std::printf("\nAll done. EXR sources remain in their directories.\n");
    std::printf("To delete after confirming MP4s look right:  rmdir /S <dir>\n\n");
    std::fflush(stdout);
}

} // anonymous namespace

// ──────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    g_cfg = parse_cli(argc, argv);

#ifdef _WIN32
    char exePath[1024] = {0};
    GetModuleFileNameA(nullptr, exePath, sizeof exePath);
    std::string ep = exePath;
    size_t slash = ep.find_last_of("\\/");
    if (slash != std::string::npos) g_shader_base = ep.substr(0, slash + 1);

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // Raise the system timer resolution so std::this_thread::sleep_for can
    // honour sub-15ms sleeps — needed for the 60fps frame-pacing loop below.
    timeBeginPeriod(1);
#endif

    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = nullptr;
    if (g_cfg.fullscreen) {
        // Borderless fullscreen at the primary monitor's native mode.
        // Works correctly on ultrawide/unusual resolutions because we just
        // read whatever the OS reports as current.
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        g_cfg.dispW = mode->width;
        g_cfg.dispH = mode->height;
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        win = glfwCreateWindow(g_cfg.dispW, g_cfg.dispH,
                               "video feedback", nullptr, nullptr);
        if (win) glfwSetWindowPos(win, 0, 0);
        printf("[window] fullscreen %dx%d @ %dHz\n",
               g_cfg.dispW, g_cfg.dispH, mode->refreshRate);
    } else {
        win = glfwCreateWindow(g_cfg.dispW, g_cfg.dispH,
                               "video feedback", nullptr, nullptr);
        printf("[window] %dx%d\n", g_cfg.dispW, g_cfg.dispH);
    }
    if (!win) { fprintf(stderr, "window failed\n"); return 1; }
    S.win = win;
    S.isFullscreen = g_cfg.fullscreen;

    glfwMakeContextCurrent(win);
    glfwSwapInterval(g_cfg.vsync ? 1 : 0);
    printf("[vsync] %s\n", g_cfg.vsync ? "on" : "off");

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "glew failed\n"); return 1; }

    // Default sim resolution: match display if user didn't override.
    if (g_cfg.simW == 0 || g_cfg.simH == 0) {
        g_cfg.simW = g_cfg.dispW;
        g_cfg.simH = g_cfg.dispH;
    }
    S.simW = g_cfg.simW; S.simH = g_cfg.simH;
    S.winW = g_cfg.dispW; S.winH = g_cfg.dispH;
    S.blurQ = g_cfg.blurQ;
    S.caQ   = g_cfg.caQ;
    S.noiseQ = g_cfg.noiseQ;
    S.activeFields = g_cfg.fields;

    const char* blurNames[]  = { "5-tap cross", "9-tap gauss", "25-tap gauss" };
    const char* caNames[]    = { "3-sample", "5-sample ramp", "8-sample wavelen" };
    const char* noiseNames[] = { "white", "pink 1/f" };
    printf("[sim] %dx%d  precision=RGBA%dF  iters=%d\n",
           S.simW, S.simH, g_cfg.precision, g_cfg.iters);
    printf("[quality] blur=%s  ca=%s  noise=%s  fields=%d\n",
           blurNames[S.blurQ], caNames[S.caQ], noiseNames[S.noiseQ], S.activeFields);

    glfwSetKeyCallback(win, key_cb);
    glfwSetFramebufferSizeCallback(win, size_cb);

    progFeedback = build_feedback_program();
    progBlit     = build_blit_program();
    if (!progFeedback || !progBlit) {
        fprintf(stderr, "[shaders] initial build failed — aborting\n");
        return 1;
    }

    GLuint mainVAO; glGenVertexArrays(1, &mainVAO); glBindVertexArray(mainVAO);

    if (!S.ov.init()) {
        fprintf(stderr, "[overlay] init failed (continuing without overlay)\n");
    }
    glBindVertexArray(mainVAO);

    // Create simulation FBOs at the simulation resolution, ONCE.
    for (int f = 0; f < S.activeFields; f++) {
        resize_fbo(S.field[f][0], S.simW, S.simH);
        resize_fbo(S.field[f][1], S.simW, S.simH);
    }

    int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
    S.winW = fbw; S.winH = fbh;
    S.ov.resize(fbw, fbh);

    // Camera setup (optional).
    if (S.cam.open(640, 480)) {
        S.camBuf.resize(S.cam.width() * S.cam.height() * 3);
        glGenTextures(1, &S.camTex);
        glBindTexture(GL_TEXTURE_2D, S.camTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, S.cam.width(), S.cam.height(),
                     0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        S.camReady = true;
    }

    print_help();
    print_status();

    preset_rescan();
    printf("[presets] %zu file(s) in %s/\n", g_presetFiles.size(), preset_dir().c_str());

    if (g_cfg.playerFps > 0)
        printf("[fps] vsync %s, cap %d fps\n", g_cfg.vsync ? "on" : "off", g_cfg.playerFps);
    else
        printf("[fps] vsync %s, uncapped\n", g_cfg.vsync ? "on" : "off");

    while (!glfwWindowShouldClose(win)) {
        const double frameStart = glfwGetTime();
        glfwPollEvents();

        glBindVertexArray(mainVAO);

        if (S.needClear) {
            for (int f = 0; f < S.activeFields; f++) {
                clear_fbo(S.field[f][0]);
                clear_fbo(S.field[f][1]);
            }
            S.needClear = false;
        }

        if (S.camReady && S.cam.grab(S.camBuf.data())) {
            glBindTexture(GL_TEXTURE_2D, S.camTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            S.cam.width(), S.cam.height(),
                            GL_RGB, GL_UNSIGNED_BYTE, S.camBuf.data());
        }

        if (!S.paused) {
            // Sub-frame iterations: run the feedback shader N times per
            // displayed frame. Each iteration advances the whole ring of
            // active fields, each reading its neighbour in the ring.
            //
            // Ring topology: field i's "other" (coupling) texture is field
            // (i+1) mod N. At N=1 this degenerates to "other = self", so
            // the couple layer becomes a no-op even when toggled on. At
            // N=4 you get the full Kaneko CML regime.
            for (int it = 0; it < g_cfg.iters; it++) {
                int srcSlot = S.writeA ? 1 : 0;
                int dstSlot = S.writeA ? 0 : 1;
                const int N = S.activeFields;
                for (int f = 0; f < N; f++) {
                    int otherIdx = (f + 1) % N;
                    render_field(f,
                                 S.field[f][srcSlot],
                                 S.field[f][dstSlot],
                                 S.field[otherIdx][srcSlot]);
                }
                S.writeA = !S.writeA;
                S.frame++;
            }
        }

        // Blit field 0 (displayed field) to the screen.
        int srcSlot = S.writeA ? 1 : 0;
        FBO& latest = S.field[0][srcSlot];
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, S.winW, S.winH);
        glUseProgram(progBlit);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, latest.tex);
        glUniform1i(glGetUniformLocation(progBlit, "uSrc"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Record from the sim-resolution texture (not the display framebuffer)
        // so recordings get the full internal quality regardless of window size.
        if (S.rec.active()) S.rec.capture(latest.fbo);

        if (S.ov.helpVisible()) S.ov.setHelpText(build_help_text());
        S.ov.draw();

        glfwSwapBuffers(win);

        S.p.inject *= 0.85f;
        if (S.p.inject < 0.001f) S.p.inject = 0.0f;

        // Pace the loop. Vsync alone lets the GPU race ahead on high-refresh
        // monitors; during recording the readback path tends to settle around
        // ~120 fps, so the captured sequence drifts off its metadata rate.
        // Sleep most of the remaining slice, spin the last ~2 ms for jitter.
        if (g_cfg.playerFps > 0) {
            const double period = 1.0 / (double)g_cfg.playerFps;
            const double deadline = frameStart + period;
            double remain = deadline - glfwGetTime();
            if (remain > 0.003)
                std::this_thread::sleep_for(std::chrono::duration<double>(remain - 0.002));
            while (glfwGetTime() < deadline) { /* spin */ }
        }
    }

    if (S.rec.active()) {
        S.rec.stop();
        if (!S.rec.lastDir().empty())
            S.recordingsThisSession.push_back(S.rec.lastDir());
    }
    S.ov.shutdown();

    // Release GL resources before tearing down the context. Fields are
    // created lazily, so zero-check each slot before deleting.
    for (int f = 0; f < 4; f++) {
        for (int s = 0; s < 2; s++) {
            if (S.field[f][s].fbo) glDeleteFramebuffers(1, &S.field[f][s].fbo);
            if (S.field[f][s].tex) glDeleteTextures(1, &S.field[f][s].tex);
        }
    }
    if (S.camTex)      glDeleteTextures(1, &S.camTex);
    if (mainVAO)       glDeleteVertexArrays(1, &mainVAO);
    if (progFeedback)  glDeleteProgram(progFeedback);
    if (progBlit)      glDeleteProgram(progBlit);

    glfwDestroyWindow(win);
    glfwTerminate();
#ifdef _WIN32
    timeEndPeriod(1);
#endif

    // Post-session encode prompt — offer to convert each EXR sequence
    // captured this session to MP4 via ffmpeg.
    encode_prompt(S.recordingsThisSession);
    return 0;
}
