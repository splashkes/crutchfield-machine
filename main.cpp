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
  #include <shellapi.h>
  #include <mmsystem.h>
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
#include <iostream>

#include "camera.h"
#include "recorder.h"
#include "overlay.h"
#include "input.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_WARNING
#include "stb_image_write.h"

// ── runtime config (from CLI) ─────────────────────────────────────────────
struct Cfg {
    int  simW = 0, simH = 0;        // 0 means "match display"
    int  dispW = 1280, dispH = 720;
    bool fullscreen = false;
    int  precision = 32;            // 8, 16, or 32 (RGBA8 / RGBA16F / RGBA32F)
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
    std::string preset;             // preset name or path to load at startup
    // Auto-demo mode — unattended installations / gallery use.
    float demoPresetSec = 0.0f;     // >0 = cycle to next preset every N seconds
    float demoInjectSec = 0.0f;     // >0 = fire an injection every N seconds
};

static void print_cli_help() {
    printf(
      "Usage: feedback.exe [options]\n"
      "  --sim-res WxH       internal simulation resolution (default: match display)\n"
      "  --display-res WxH   window size in windowed mode (default: 1280x720)\n"
      "  --fullscreen        borderless fullscreen at monitor's native resolution\n"
      "  --precision 8|16|32 feedback buffer format: RGBA8 / RGBA16F / RGBA32F (default: 32)\n"
      "                       (8 = unorm — simulates 8-bit HDMI capture in the feedback loop)\n"
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
      "  --preset NAME       load preset at startup (bare name or path; .ini optional)\n"
      "  --demo-presets S    auto-cycle to next preset every S seconds (0 = off)\n"
      "  --demo-inject S     auto-fire a random injection every S seconds (0 = off)\n"
      "  --demo              shortcut for --demo-presets 30 --demo-inject 8\n"
      "  -h, --help          show this help\n\n"
      "Launch with NO arguments to get an interactive mode picker — handy for\n"
      "non-CLI use and for double-clicking the exe from Explorer.\n\n"
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
        else if (eq("--precision"))    { int p = atoi(next()); if (p==8||p==16||p==32) c.precision = p; }
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
        else if (eq("--preset"))       { c.preset = next(); }
        else if (eq("--demo-presets")) { c.demoPresetSec = (float)atof(next()); if (c.demoPresetSec < 0) c.demoPresetSec = 0; }
        else if (eq("--demo-inject"))  { c.demoInjectSec = (float)atof(next()); if (c.demoInjectSec < 0) c.demoInjectSec = 0; }
        else if (eq("--demo"))         { c.demoPresetSec = 30.0f; c.demoInjectSec = 8.0f; }
        else if (eq("-h") || eq("--help")) { print_cli_help(); exit(0); }
        else { fprintf(stderr, "[cli] unknown arg: %s\n", argv[i]); print_cli_help(); exit(1); }
    }
    // If user didn't pass --rec-fps, use the player cap (or 60 when uncapped).
    if (c.recFps == 0) c.recFps = c.playerFps > 0 ? c.playerFps : 60;
    return c;
}

// File-scope cfg, so helper functions declared before State can reference it.
static Cfg g_cfg;

static const char* precision_label(int p) {
    return p == 8 ? "RGBA8" : p == 32 ? "RGBA32F" : "RGBA16F";
}

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

    // V-4 effect slots — two slots, each can host any of 18 effects.
    //   vfxSlot ∈ [0..18]; 0 means "off".
    //   vfxParam is the CONTROL-dial-equivalent continuous parameter, 0..1.
    //   vfxBSource ∈ {0 = camera, 1 = self (current image reprocessed)}
    //     for key/PinP families that need a second source.
    int   vfxSlot[2]    = { 0, 0 };
    float vfxParam[2]   = { 0.5f, 0.5f };
    int   vfxBSource[2] = { 0, 0 };

    // Output fade — bipolar, -1 = full black, 0 = pass-through, +1 = white.
    float outFade = 0.0f;

    // BPM sync. Tap tempo fills taps[]; bpm is smoothed across the
    // last few taps. beatPhase in [0, 1) is the fractional position
    // within the current beat. divIdx selects division (see BPM_DIVS).
    float bpm          = 120.0f;
    double beatOrigin  = 0.0;    // glfwGetTime() reference for beat 0
    float beatPhase    = 0.0f;   // cached; updated in update_bpm
    int   divIdx       = 1;      // 0 = x2 (double tempo)
                                 // 1 = x1 (every beat)
                                 // 2 = half-tempo
                                 // 3 = quarter-tempo
    bool  bpmSyncOn    = false;
    bool  bpmInject    = true;   // inject on beat (starts on once sync on)
    bool  bpmStrobe    = true;   // strobe rate lock
    bool  bpmVfxCycle  = false;  // auto-cycle vfx slot 1
    bool  bpmFlash     = false;  // flash fade on beat
    bool  bpmDecayDip  = false;  // momentary decay dip

    // Transients driven by beat events. Not user-facing.
    float decayDipTimer = 0.0f;  // seconds remaining in active dip
    float flashDecay    = 0.0f;  // decaying flash magnitude (signed)
};

// BPM division multipliers (applied to the 60/bpm base period).
static constexpr float BPM_DIV_MUL[] = { 0.5f, 1.0f, 2.0f, 4.0f };
static const char* BPM_DIV_NAMES[] = { "x2 (fast)", "x1", "÷2", "÷4" };
static constexpr int N_BPM_DIVS = 4;

// Effect catalogue shared between host, shader, and UI. Index 0 is "off".
// Keep in sync with vfx_apply's switch in shaders/layers/vfx_slot.glsl.
static const char* VFX_NAMES[] = {
    "off",        "Strobe",    "Still",      "Shake",
    "Negative",   "Colorize",  "Monochrome", "Posterize",
    "ColorPass",  "Mirror-H",  "Mirror-V",   "Mirror-HV",
    "Multi-H",    "Multi-V",   "Multi-HV",
    "W-LumiKey",  "B-LumiKey", "ChromaKey",  "PinP",
};
static constexpr int VFX_COUNT = (int)(sizeof(VFX_NAMES) / sizeof(VFX_NAMES[0]));

static const char* VFX_BSRC_NAMES[] = { "camera", "self-reproc" };

// ── framebuffer-object helper ─────────────────────────────────────────────
struct FBO { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };

static void resize_fbo(FBO& f, int w, int h) {
    if (!f.fbo) glGenFramebuffers(1, &f.fbo);
    if (!f.tex) glGenTextures(1, &f.tex);
    f.w = w; f.h = h;
    glBindTexture(GL_TEXTURE_2D, f.tex);
    GLenum internalFmt, type;
    switch (g_cfg.precision) {
        case 8:  internalFmt = GL_RGBA8;    type = GL_UNSIGNED_BYTE; break;
        case 32: internalFmt = GL_RGBA32F;  type = GL_FLOAT;         break;
        default: internalFmt = GL_RGBA16F;  type = GL_HALF_FLOAT;    break;
    }
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

    // Armed-layer cursor for the Layers section. Indexes into LAYERS[].
    int  armedLayer = 0;
    // Armed-quality cursor for the Quality section. 0..3.
    int  armedQuality = 0;

    // Screenshot request — set by key handler, consumed in render loop.
    bool screenshotPending = false;

    // Auto-demo mode. Zero = disabled; positive = seconds between firings.
    // `demoLastPreset` / `demoLastInject` track the last time each fired.
    float demoPresetSec = 0.0f;
    float demoInjectSec = 0.0f;
    double demoLastPreset = 0.0;
    double demoLastInject = 0.0;
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
      " PrtSc    screenshot (PNG, sim resolution, no HUD)\n"
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
"swirl  = %.6f\n"
"\n"
"[vfx]\n"
"# V-4-inspired effect slots. slot1/slot2 are effect IDs (0=off, 1..18).\n"
"# See main.cpp::VFX_NAMES for the full list. param1/param2 are the\n"
"# CONTROL-dial equivalents (0..1). bsource1/bsource2: 0=camera, 1=self.\n"
"slot1    = %d\n"
"param1   = %.6f\n"
"bsource1 = %d\n"
"slot2    = %d\n"
"param2   = %.6f\n"
"bsource2 = %d\n"
"\n"
"[output]\n"
"# Output fade: bipolar, -1=black, 0=through, +1=white.\n"
"fade     = %.6f\n"
"\n"
"[bpm]\n"
"# Tempo + 5 beat-locked modulations. divIdx: 0=x2, 1=x1, 2=÷2, 3=÷4.\n"
"bpm      = %.3f\n"
"divIdx   = %d\n"
"sync     = %s\n"
"inject   = %s\n"
"strobe   = %s\n"
"vfxCycle = %s\n"
"flash    = %s\n"
"decayDip = %s\n",
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
        p.thermAmp, p.thermScale, p.thermSpeed, p.thermRise, p.thermSwirl,
        p.vfxSlot[0], p.vfxParam[0], p.vfxBSource[0],
        p.vfxSlot[1], p.vfxParam[1], p.vfxBSource[1],
        p.outFade,
        p.bpm, p.divIdx,
        p.bpmSyncOn   ? "on" : "off",
        p.bpmInject   ? "on" : "off",
        p.bpmStrobe   ? "on" : "off",
        p.bpmVfxCycle ? "on" : "off",
        p.bpmFlash    ? "on" : "off",
        p.bpmDecayDip ? "on" : "off");
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
        } else if (section == "vfx") {
            int n = atoi(v.c_str());
            float fv = (float)atof(v.c_str());
            auto clamp_slot = [](int x) { return x < 0 ? 0 : (x >= VFX_COUNT ? 0 : x); };
            if      (k == "slot1")    p.vfxSlot[0]    = clamp_slot(n);
            else if (k == "slot2")    p.vfxSlot[1]    = clamp_slot(n);
            else if (k == "param1")   p.vfxParam[0]   = fmaxf(0.f, fminf(1.f, fv));
            else if (k == "param2")   p.vfxParam[1]   = fmaxf(0.f, fminf(1.f, fv));
            else if (k == "bsource1") p.vfxBSource[0] = (n & 1);
            else if (k == "bsource2") p.vfxBSource[1] = (n & 1);
        } else if (section == "output") {
            float fv = (float)atof(v.c_str());
            if (k == "fade") p.outFade = fmaxf(-1.f, fminf(1.f, fv));
        } else if (section == "bpm") {
            if      (k == "bpm")     p.bpm = fmaxf(30.f, fminf(300.f, (float)atof(v.c_str())));
            else if (k == "divIdx")  { int n = atoi(v.c_str()); if (n>=0 && n<N_BPM_DIVS) p.divIdx = n; }
            else if (k == "sync")    p.bpmSyncOn   = parse_bool_onoff(v);
            else if (k == "inject")  p.bpmInject   = parse_bool_onoff(v);
            else if (k == "strobe")  p.bpmStrobe   = parse_bool_onoff(v);
            else if (k == "vfxCycle")p.bpmVfxCycle = parse_bool_onoff(v);
            else if (k == "flash")   p.bpmFlash    = parse_bool_onoff(v);
            else if (k == "decayDip")p.bpmDecayDip = parse_bool_onoff(v);
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

// Resolve a --preset argument to a real path in g_presetFiles, or "" if no
// match. Accepts: bare stem ("03_turing_blobs"), filename with extension, or
// a direct absolute/relative path. Match is exact on stem or filename.
// Must be called after preset_rescan() so g_presetFiles is populated.
static std::string preset_resolve(const std::string& arg) {
    if (arg.empty()) return "";
    // Direct path (works for absolute or cwd-relative).
    if (fs::exists(arg)) return arg;
    // Exact stem or filename match against scanned presets.
    for (const auto& p : g_presetFiles) {
        fs::path fp(p);
        if (fp.stem().string() == arg || fp.filename().string() == arg) return p;
    }
    // Try "<name>.ini" under presets/ if user dropped the extension.
    fs::path tryPath = fs::path(preset_dir()) / (arg + ".ini");
    if (fs::exists(tryPath)) return tryPath.string();
    return "";
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

// ── screenshots ──────────────────────────────────────────────────────────
// One-shot PNG capture of the current sim-resolution frame. Reads from the
// field FBO directly (not the display), so screenshots are always at full
// sim quality regardless of window size, and never include the HUD overlay.
static std::string screenshot_dir() {
    return g_shader_base.empty() ? "screenshots" : (g_shader_base + "screenshots");
}

static bool save_screenshot(GLuint fbo, int w, int h) {
    std::vector<uint8_t> buf((size_t)w * h * 3);
    GLint prev = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev);

    // GL origin is bottom-left; PNG origin is top-left. Flip row-by-row.
    const size_t row = (size_t)w * 3;
    std::vector<uint8_t> flipped(buf.size());
    for (int y = 0; y < h; y++)
        std::memcpy(flipped.data() + (size_t)y * row,
                    buf.data()     + (size_t)(h - 1 - y) * row, row);

    fs::path dir = screenshot_dir();
    if (!fs::exists(dir)) fs::create_directory(dir);
    char ts[64];
    time_t t = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    strftime(ts, sizeof ts, "shot_%Y%m%d_%H%M%S.png", &lt);
    fs::path full = dir / ts;
    int ok = stbi_write_png(full.string().c_str(), w, h, 3,
                            flipped.data(), (int)row);
    if (ok) printf("[shot] %s\n", full.string().c_str());
    else    fprintf(stderr, "[shot] failed to write %s\n", full.string().c_str());
    return ok != 0;
}

// ── music-mode helpers ───────────────────────────────────────────────────
// Glue between the app and a running Strudel session.
//
// How the integration works: feedback.exe registers itself as a virtual
// MIDI port called "feedback" via teVirtualMIDI (the driver ships with
// loopMIDI and is auto-loaded at boot). Strudel's Web MIDI sees the port
// directly — the user just writes `.midi("feedback")` and it works.
// The loopMIDI GUI never needs to open.
//
// The only thing we might need to do at runtime: install the driver if
// it's missing. That's a one-time winget step.

#ifdef _WIN32
static bool driver_installed() {
    // teVirtualMIDI64.dll ships with loopMIDI and lives in System32 after
    // install. Presence of the file is our "driver ready" signal.
    return GetFileAttributesA("C:\\Windows\\System32\\teVirtualMIDI64.dll")
           != INVALID_FILE_ATTRIBUTES;
}

// winget-based installer for the loopMIDI package, which contains the
// driver we actually want. UAC prompt appears; all other agreements
// auto-accepted. Blocks up to ~3 min.
static bool winget_install(const char* packageId) {
    std::printf("[music] invoking winget to install %s (accept the UAC prompt)…\n",
                packageId);
    char cmdline[512];
    std::snprintf(cmdline, sizeof cmdline,
        "install --id %s --silent "
        "--accept-package-agreements --accept-source-agreements",
        packageId);
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize       = sizeof sei;
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = "open";
    sei.lpFile       = "winget";
    sei.lpParameters = cmdline;
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei) || !sei.hProcess) {
        std::fprintf(stderr, "[music] winget not available on this system\n");
        return false;
    }
    DWORD wait = WaitForSingleObject(sei.hProcess, 180000);
    DWORD exitCode = 1;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    if (wait != WAIT_OBJECT_0) {
        std::fprintf(stderr, "[music] winget install timed out\n");
        return false;
    }
    if (exitCode == 0 || exitCode == 0x8A15002B) {   // 0x8A15002B = already installed
        std::printf("[music] winget install succeeded (exit 0x%lx)\n",
                    (unsigned long)exitCode);
        return true;
    }
    std::fprintf(stderr, "[music] winget install failed (exit 0x%lx)\n",
                 (unsigned long)exitCode);
    return false;
}

// Ensure the teVirtualMIDI driver is present. If missing, winget-install
// loopMIDI (which bundles the driver). Returns true if the driver is
// ready to use.
static bool music_ensure_driver() {
    if (driver_installed()) return true;
    std::printf("[music] MIDI driver not found — installing loopMIDI "
                "(its driver is what Strudel will route through)…\n");
    if (!winget_install("TobiasErichsen.loopMIDI")) {
        std::fprintf(stderr,
            "[music] Couldn't install automatically. Install manually from:\n"
            "        https://www.tobias-erichsen.de/software/loopmidi.html\n");
        ShellExecuteA(nullptr, "open",
            "https://www.tobias-erichsen.de/software/loopmidi.html",
            nullptr, nullptr, SW_SHOWNORMAL);
        return false;
    }
    // Post-install verify.
    if (!driver_installed()) {
        std::fprintf(stderr,
            "[music] winget reported success but driver DLL is still missing.\n"
            "        A reboot may be required to finish driver registration.\n");
        return false;
    }
    std::printf("[music] driver installed. Virtual port will activate now.\n");
    return true;
}

static void music_open_strudel() {
    HINSTANCE r = ShellExecuteA(nullptr, "open", "https://strudel.cc/",
                                nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        std::fprintf(stderr, "[music] couldn't open browser (%ld)\n",
                     (long)(INT_PTR)r);
    else
        std::printf("[music] opened https://strudel.cc/ — use .midi(\"feedback\")\n"
                    "        in your pattern to route to this app.\n");
}

// Passive hint on every launch when the driver's absent. Silent when
// everything's ready to go.
static void music_startup_hint() {
    if (driver_installed()) return;
    std::printf(
        "\n[music] MIDI driver not present — Strudel integration is one click away:\n"
        "        Press Ctrl+M in-app to winget-install the driver (free, ~1 MB),\n"
        "        then the port 'feedback' appears automatically in Strudel.\n"
        "        Pattern side: `s(\"bd\").midi(\"feedback\")`\n"
        "        Startup picker's 'Music mode' (#7) does this plus opens Strudel.\n\n");
}
#else
static bool music_ensure_driver() { return false; }
static void music_open_strudel() {}
static void music_startup_hint() {}
#endif

// ── startup mode picker ──────────────────────────────────────────────────
// Shown only when the app is launched with no CLI args — i.e. double-clicked
// from Explorer. Gives non-CLI users a one-keystroke way to pick a common
// mode. Any CLI arg at all skips this path so existing workflows stay intact.
//
// Implementation: console prompt. On Windows, double-clicking a console-
// subsystem .exe pops a terminal window automatically; the menu lives there.
// It's not pretty, but it's zero deps and works identically on every machine.
static void run_mode_picker(Cfg& c) {
    preset_rescan();

    while (true) {
        printf("\n");
        printf("========================================================\n");
        printf("    Crutchfield Machine  —  video feedback, pure and simple\n");
        printf("========================================================\n\n");
        printf("  1  Default        windowed, moderate quality\n");
        printf("  2  Fullscreen     borderless at native resolution\n");
        printf("  3  Gallery mode   fullscreen + auto-cycle presets + inject\n");
        printf("  4  4K Fullscreen  full float 3840x2160 sim\n");
        printf("  5  8-bit study    RGBA8 feedback loop (quantization demo)\n");
        printf("  6  Load preset... pick from the %zu preset(s) shipped\n",
               g_presetFiles.size());
        printf("  7  Music mode     fullscreen + launch loopMIDI + open Strudel\n");
        printf("\n  Q  Quit\n\n");
        printf("  Tip: run with --help for the full list of flags.\n\n");
        printf("Choice [1]: ");
        fflush(stdout);

        std::string line;
        if (!std::getline(std::cin, line)) return;              // EOF → defaults
        char ch = line.empty() ? '1' : (char)tolower((unsigned char)line[0]);

        switch (ch) {
            case '1': return;
            case '2': c.fullscreen = true; return;
            case '3': c.fullscreen = true;
                      c.demoPresetSec = 30.0f;
                      c.demoInjectSec = 8.0f; return;
            case '4': c.fullscreen = true;
                      c.simW = 3840; c.simH = 2160; return;
            case '5': c.precision = 8; return;
            case '7': {
                c.fullscreen = true;
                printf("\n");
                music_ensure_driver();
                music_open_strudel();
                return;
            }
            case '6': {
                if (g_presetFiles.empty()) {
                    printf("\n  No presets found in %s/\n", preset_dir().c_str());
                    continue;
                }
                printf("\n");
                for (size_t i = 0; i < g_presetFiles.size(); i++) {
                    fs::path p = g_presetFiles[i];
                    printf("  %2zu  %s\n", i + 1, p.stem().string().c_str());
                }
                printf("\nPreset number [1]: ");
                fflush(stdout);
                std::getline(std::cin, line);
                int idx = line.empty() ? 1 : atoi(line.c_str());
                if (idx >= 1 && idx <= (int)g_presetFiles.size()) {
                    c.preset = g_presetFiles[idx - 1];
                    return;
                }
                printf("  (invalid — showing menu again)\n");
                continue;
            }
            case 'q': exit(0);
            default:
                printf("  (unrecognised — try 1-6 or Q)\n");
                continue;
        }
    }
}

// Track frame timing for FPS readout in the help overlay.
static double g_lastFrameTime = 0.0;
static double g_smoothedFps   = 60.0;

// Translate a GLFW key + mods back into a short human spec (e.g. "Ctrl+S",
// "F1", "Q"). Mirror of input.cpp's key_spec_string, kept local so the
// help panel can format binding summaries without exposing that helper.
static std::string fmt_key(int key, int mods) {
    std::string s;
    if (mods & GLFW_MOD_CONTROL) s += "Ctrl+";
    if (mods & GLFW_MOD_ALT)     s += "Alt+";
    if (mods & GLFW_MOD_SHIFT)   s += "Shift+";
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) { s += (char)('A' + (key - GLFW_KEY_A)); return s; }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) { s += (char)('0' + (key - GLFW_KEY_0)); return s; }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        char b[8]; snprintf(b, sizeof b, "F%d", 1 + (key - GLFW_KEY_F1));
        return s + b;
    }
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
        char b[8]; snprintf(b, sizeof b, "NP%d", (key - GLFW_KEY_KP_0));
        return s + b;
    }
    switch (key) {
        case GLFW_KEY_SPACE: return s + "Space";
        case GLFW_KEY_ENTER: return s + "Enter";
        case GLFW_KEY_ESCAPE: return s + "Esc";
        case GLFW_KEY_TAB: return s + "Tab";
        case GLFW_KEY_INSERT: return s + "Ins";
        case GLFW_KEY_DELETE: return s + "Del";
        case GLFW_KEY_HOME: return s + "Home";
        case GLFW_KEY_END: return s + "End";
        case GLFW_KEY_PAGE_UP: return s + "PgUp";
        case GLFW_KEY_PAGE_DOWN: return s + "PgDn";
        case GLFW_KEY_LEFT: return s + "Left";
        case GLFW_KEY_RIGHT: return s + "Right";
        case GLFW_KEY_UP: return s + "Up";
        case GLFW_KEY_DOWN: return s + "Down";
        case GLFW_KEY_LEFT_BRACKET: return s + "[";
        case GLFW_KEY_RIGHT_BRACKET: return s + "]";
        case GLFW_KEY_SEMICOLON: return s + ";";
        case GLFW_KEY_APOSTROPHE: return s + "'";
        case GLFW_KEY_COMMA: return s + ",";
        case GLFW_KEY_PERIOD: return s + ".";
        case GLFW_KEY_MINUS: return s + "-";
        case GLFW_KEY_EQUAL: return s + "=";
        case GLFW_KEY_SLASH: return s + "/";
        case GLFW_KEY_BACKSLASH: return s + "\\";
        case GLFW_KEY_GRAVE_ACCENT: return s + "`";
    }
    return s + "?";
}

// Return the keyboard binding for an action as "Q" or "Ctrl+S". Only the
// first matching keyboard binding is returned (bindings.ini can add more).
static std::string keys_for(ActionId a) {
    for (const Binding& b : g_input.bindings()) {
        if (b.action == a && b.source == SRC_KEY)
            return fmt_key(b.code, b.modmask);
    }
    return "";
}

// "Q/A" for up/down pairs. Falls back to just one or the other if one is
// unbound. If both show the same chord prefix we don't try to be clever —
// the user just sees "Ctrl+Up / Ctrl+Dn" which is fine.
static std::string keys_pair(ActionId up, ActionId dn) {
    std::string a = keys_for(up), b = keys_for(dn);
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + "/" + b;
}

// ── help section providers ──────────────────────────────────────────────
// Ordered names shown in the top-level menu. The index is what gets passed
// to the provider below.
static const char* HELP_SECTIONS[] = {
    "Status", "Layers", "Warp", "Optics", "Color",
    "Dynamics", "Physics", "Thermal", "Inject",
    "VFX-1", "VFX-2", "Output", "BPM",
    "Quality", "App", "Bindings",
};
static constexpr int N_HELP_SECTIONS =
    (int)(sizeof(HELP_SECTIONS) / sizeof(HELP_SECTIONS[0]));

// Helpers used inside section builders
static std::string section_status() {
    const auto& p = S.p;
    double now = glfwGetTime();
    double dt  = now - g_lastFrameTime;
    if (dt > 0.0 && dt < 1.0) {
        double inst = 1.0 / dt;
        g_smoothedFps = g_smoothedFps * 0.9 + inst * 0.1;
    }
    g_lastFrameTime = now;

    const char* patNames[] = { "H-bars","V-bars","dot","checker","gradient" };
    const char* rec = S.rec.active()
        ? (S.rec.queueDropped() ? "REC (DROPS)"
            : (S.rec.queueDepth() >= 3 ? "REC (slow)" : "REC"))
        : "off";
    const std::string preset = g_presetFiles.empty() ? "(none)"
                             : (g_currentPreset < 0 ? "(unsaved)"
                                : preset_current_name());
    char buf[2048];
    snprintf(buf, sizeof buf,
        "sim     : %d x %d  RGBA%dF\n"
        "window  : %d x %d  %s\n"
        "fps     : %.1f   iters: %d\n"
        "recording: %s\n"
        "paused  : %s\n"
        "preset  : %s\n"
        "pattern : %s\n"
        "fields  : %d\n"
        "\n"
        "-- quick glance --\n"
        "zoom %.4f  theta %+.3f rad/pass\n"
        "decay %.4f  noise %.4f\n"
        "couple %.3f  ext (cam) %.3f",
        S.simW, S.simH, g_cfg.precision,
        S.winW, S.winH, S.isFullscreen ? "FULLSCREEN" : "windowed",
        g_smoothedFps, g_cfg.iters, rec,
        S.paused ? "YES" : "no", preset.c_str(),
        patNames[(unsigned)p.pattern % 5], S.activeFields,
        p.zoom, p.theta, p.decay, p.noise,
        p.couple, p.external);
    return buf;
}

static std::string section_layers() {
    // Iterate LAYERS[] so the cursor-armed row matches the index used by
    // the gamepad navigation handlers. Also list the keyboard Fn/Ins/PgDn
    // bindings next to each layer for quick reference.
    static const char* kbdMap[] = {
        "F1 ", "F2 ", "F3 ", "F4 ", "F5 ", "F6 ", "F7 ", "F8 ",
        "F9 ", "F10", "Ins", "PgD"
    };
    const int N = sizeof(LAYERS) / sizeof(LAYERS[0]);
    std::string out;
    char row[96];
    for (int i = 0; i < N; i++) {
        const LayerInfo& li = LAYERS[i];
        bool on = (S.enable & li.bit) != 0;
        const char* cursor = (i == S.armedLayer) ? "\x10" : " ";
        snprintf(row, sizeof row, "%s  %s  %-9s  %s\n",
                 cursor, kbdMap[i], li.name, on ? "ON " : "off");
        out += row;
    }
    return out;
}

static std::string section_warp() {
    const auto& p = S.p;
    char b[1024];
    snprintf(b, sizeof b,
        "%-5s  zoom      : %.4f /pass\n"
        "%-5s  rotation  : %+.4f rad/pass  (%+0.1f deg/s @60fps)\n"
        "%-5s  translate : (%+.4f, %+.4f)\n"
        "\n"
        "Shift  coarse (20x step)\n"
        "Gamepad: LStick translates; RStick-X rotates.",
        keys_pair(ACT_ZOOM_UP, ACT_ZOOM_DN).c_str(), p.zoom,
        keys_pair(ACT_THETA_UP, ACT_THETA_DN).c_str(), p.theta, p.theta * 57.2958f * 60.0f,
        "arr", p.transX, p.transY);
    return b;
}

static std::string section_optics() {
    const auto& p = S.p;
    char b[1024];
    snprintf(b, sizeof b,
        "%-5s  chroma    : %+.4f\n"
        "%-5s  blur-X    : %.2f px\n"
        "%-5s  blur-Y    : %.2f px\n"
        "%-5s  blur-ang  : %+.3f rad",
        keys_pair(ACT_CHROMA_UP, ACT_CHROMA_DN).c_str(), p.chroma,
        keys_pair(ACT_BLURX_UP,  ACT_BLURX_DN ).c_str(), p.blurX,
        keys_pair(ACT_BLURY_UP,  ACT_BLURY_DN ).c_str(), p.blurY,
        keys_pair(ACT_BLURANG_UP,ACT_BLURANG_DN).c_str(), p.blurAngle);
    return b;
}

static std::string section_color() {
    const auto& p = S.p;
    char b[1024];
    snprintf(b, sizeof b,
        "%-5s  gamma     : %.3f\n"
        "%-5s  hue rate  : %+.4f /pass  (%+.2f rot/s)\n"
        "%-5s  satur     : %.3f x\n"
        "%-5s  contrast  : %.3f x",
        keys_pair(ACT_GAMMA_UP,    ACT_GAMMA_DN   ).c_str(), p.gamma,
        keys_pair(ACT_HUE_UP,      ACT_HUE_DN     ).c_str(), p.hueRate, p.hueRate * 60.0f,
        keys_pair(ACT_SAT_UP,      ACT_SAT_DN     ).c_str(), p.satGain,
        keys_pair(ACT_CONTRAST_UP, ACT_CONTRAST_DN).c_str(), p.contrast);
    return b;
}

static std::string section_dynamics() {
    const auto& p = S.p;
    char b[1024];
    snprintf(b, sizeof b,
        "%-5s  decay     : %.5f /pass\n"
        "%-5s  noise     : %.4f\n"
        "%-5s  couple    : %.3f\n"
        "%-5s  external  : %.3f",
        keys_pair(ACT_DECAY_UP,    ACT_DECAY_DN   ).c_str(), p.decay,
        keys_pair(ACT_NOISE_UP,    ACT_NOISE_DN   ).c_str(), p.noise,
        keys_pair(ACT_COUPLE_UP,   ACT_COUPLE_DN  ).c_str(), p.couple,
        keys_pair(ACT_EXTERNAL_UP, ACT_EXTERNAL_DN).c_str(), p.external);
    return b;
}

static std::string section_physics() {
    const auto& p = S.p;
    char b[1024];
    snprintf(b, sizeof b,
        "Crutchfield 1984 camera-side knobs.\n"
        "\n"
        "%-5s  invert       : %s\n"
        "%-5s  sensor gamma : %.3f (0.6..0.9 per paper)\n"
        "%-5s  sat knee     : %.3f (0 clip, 1 Reinhard)\n"
        "%-5s  color cross  : %.3f (0 indep, 1 full avg)",
        keys_for(ACT_INVERT_TOGGLE).c_str(), p.invert ? "ON" : "off",
        keys_pair(ACT_SENSORGAMMA_UP, ACT_SENSORGAMMA_DN).c_str(), p.sensorGamma,
        keys_pair(ACT_SATKNEE_UP,     ACT_SATKNEE_DN    ).c_str(), p.satKnee,
        keys_pair(ACT_COLORCROSS_UP,  ACT_COLORCROSS_DN ).c_str(), p.colorCross);
    return b;
}

static std::string section_thermal() {
    const auto& p = S.p;
    char b[1024];
    snprintf(b, sizeof b,
        "Air turbulence between camera & monitor.\n"
        "\n"
        "%-7s amplitude  : %.4f\n"
        "%-7s scale      : %.2f\n"
        "%-7s speed      : %.2f\n"
        "%-7s rise       : %+.3f\n"
        "%-7s swirl      : %.3f",
        keys_pair(ACT_THERMAMP_UP,   ACT_THERMAMP_DN  ).c_str(), p.thermAmp,
        keys_pair(ACT_THERMSCALE_UP, ACT_THERMSCALE_DN).c_str(), p.thermScale,
        keys_pair(ACT_THERMSPEED_UP, ACT_THERMSPEED_DN).c_str(), p.thermSpeed,
        keys_pair(ACT_THERMRISE_UP,  ACT_THERMRISE_DN ).c_str(), p.thermRise,
        keys_pair(ACT_THERMSWIRL_UP, ACT_THERMSWIRL_DN).c_str(), p.thermSwirl);
    return b;
}

static std::string section_inject() {
    const auto& p = S.p;
    const char* patNames[] = { "H-bars", "V-bars", "dot", "checker", "gradient" };
    auto cur = [&](int i) { return i == p.pattern ? "\x10" : " "; };
    char b[1024];
    snprintf(b, sizeof b,
        "%s  1  H-bars\n"
        "%s  2  V-bars\n"
        "%s  3  dot\n"
        "%s  4  checker\n"
        "%s  5  gradient\n"
        "\n"
        "DP-L/R cursor   A tap-inject   LT/RT hold\n"
        "%-5s  inject (hold)     F10  toggle inject layer",
        cur(0), cur(1), cur(2), cur(3), cur(4),
        keys_for(ACT_INJECT_HOLD).c_str());
    (void)patNames;
    return b;
}

static std::string section_quality() {
    const char* blur[] = {"5-tap cross","9-gauss","25-gauss"};
    const char* ca[]   = {"3-sample","5-ramp","8-wavelen"};
    const char* ns[]   = {"white","pink 1/f"};
    auto cur = [&](int i) { return i == S.armedQuality ? "\x10" : " "; };
    char b[1024];
    snprintf(b, sizeof b,
        "%s  %-5s  blur kernel : %s\n"
        "%s  %-5s  CA sampler  : %s\n"
        "%s  %-5s  noise type  : %s\n"
        "%s  %-5s  fields      : %d\n"
        "\n"
        "DP-L/R move cursor   A cycle armed",
        cur(0), keys_for(ACT_BLURQ_CYCLE).c_str(),  blur[S.blurQ],
        cur(1), keys_for(ACT_CAQ_CYCLE  ).c_str(),  ca[S.caQ],
        cur(2), keys_for(ACT_NOISEQ_CYCLE).c_str(), ns[S.noiseQ],
        cur(3), keys_for(ACT_FIELDS_CYCLE).c_str(), S.activeFields);
    return b;
}

static std::string section_app() {
    char b[1024];
    snprintf(b, sizeof b,
        "%-8s  quit\n"
        "%-8s  pause/resume\n"
        "%-8s  clear fields\n"
        "%-8s  fullscreen\n"
        "%-8s  help toggle\n"
        "%-8s  reload shaders\n"
        "%-8s  recording start/stop\n"
        "%-8s  preset save\n"
        "%-8s  next preset\n"
        "%-8s  prev preset",
        keys_for(ACT_QUIT).c_str(),
        keys_for(ACT_PAUSE).c_str(),
        keys_for(ACT_CLEAR).c_str(),
        keys_for(ACT_FULLSCREEN).c_str(),
        keys_for(ACT_HELP).c_str(),
        keys_for(ACT_RELOAD_SHADERS).c_str(),
        keys_for(ACT_REC_TOGGLE).c_str(),
        keys_for(ACT_PRESET_SAVE).c_str(),
        keys_for(ACT_PRESET_NEXT).c_str(),
        keys_for(ACT_PRESET_PREV).c_str());
    return b;
}

static std::string section_vfx(int slot) {
    const int e = S.p.vfxSlot[slot];
    const int b = S.p.vfxBSource[slot];
    const float pv = S.p.vfxParam[slot];
    ActionId next  = (slot == 0) ? ACT_VFX1_CYCLE_FWD  : ACT_VFX2_CYCLE_FWD;
    ActionId prev  = (slot == 0) ? ACT_VFX1_CYCLE_BACK : ACT_VFX2_CYCLE_BACK;
    ActionId off   = (slot == 0) ? ACT_VFX1_OFF        : ACT_VFX2_OFF;
    ActionId pup   = (slot == 0) ? ACT_VFX1_PARAM_UP   : ACT_VFX2_PARAM_UP;
    ActionId pdn   = (slot == 0) ? ACT_VFX1_PARAM_DN   : ACT_VFX2_PARAM_DN;
    ActionId bsrc  = (slot == 0) ? ACT_VFX1_BSRC_CYCLE : ACT_VFX2_BSRC_CYCLE;

    std::string out;
    char row[128];
    snprintf(row, sizeof row, "Slot %d  param=%.2f  B=%s\n\n",
             slot + 1, pv, VFX_BSRC_NAMES[b]);
    out += row;

    // Full 19-entry effect list, current marked with a cursor arrow.
    for (int i = 0; i < VFX_COUNT; i++) {
        const char* cursor = (i == e) ? "\x10" : " ";
        snprintf(row, sizeof row, "%s  %2d  %s\n", cursor, i, VFX_NAMES[i]);
        out += row;
    }
    out += "\n";
    snprintf(row, sizeof row,
        "%-7s next    %-7s prev    %-7s off\n"
        "%-7s param +/-   %-7s B-source",
        keys_for(next).c_str(),
        keys_for(prev).c_str(),
        keys_for(off).c_str(),
        keys_pair(pup, pdn).c_str(),
        keys_for(bsrc).c_str());
    out += row;
    return out;
}
static std::string section_output() {
    char b[512];
    const char* bar = (S.p.outFade <= -0.75f) ? "BLACK"
                    : (S.p.outFade <= -0.25f) ? "dark"
                    : (S.p.outFade <   0.25f) ? "through"
                    : (S.p.outFade <   0.75f) ? "bright"
                    :                             "WHITE";
    snprintf(b, sizeof b,
        "Output fade (bipolar).\n"
        "\n"
        "current : %+.2f   (%s)\n"
        "\n"
        "%-10s toward white (+)\n"
        "%-10s toward black (-)\n"
        "\n"
        "Gamepad right-stick Y maps absolute: the stick IS the fade.\n"
        "Self-centering when released.",
        S.p.outFade, bar,
        keys_for(ACT_OUTFADE_UP).c_str(),
        keys_for(ACT_OUTFADE_DN).c_str());
    return b;
}
static std::string section_bpm() {
    const auto& p = S.p;
    const auto& mi = g_input.midi();
    char midiLine[256];
    if (mi.connected && mi.clockLive) {
        snprintf(midiLine, sizeof midiLine,
            "MIDI     : '%s'  clock %.1f bpm  LOCKED",
            mi.portName.c_str(), mi.derivedBpm);
    } else if (mi.connected) {
        snprintf(midiLine, sizeof midiLine,
            "MIDI     : '%s'  no clock (tap active)",
            mi.portName.c_str());
    } else {
        snprintf(midiLine, sizeof midiLine,
            "MIDI     : no port — press %s to install driver",
            keys_for(ACT_LAUNCH_LOOPMIDI).c_str());
    }
    char lastLine[160] = "";
    if (mi.lastNoteNum >= 0 || mi.lastCcNum >= 0) {
        if (mi.lastNoteNum >= 0 && mi.lastCcNum >= 0) {
            snprintf(lastLine, sizeof lastLine,
                "           last note %d ch%d vel%d   cc %d=%d ch%d\n",
                mi.lastNoteNum, mi.lastNoteCh, mi.lastNoteVel,
                mi.lastCcNum, mi.lastCcVal, mi.lastCcCh);
        } else if (mi.lastNoteNum >= 0) {
            snprintf(lastLine, sizeof lastLine,
                "           last note %d ch%d vel%d\n",
                mi.lastNoteNum, mi.lastNoteCh, mi.lastNoteVel);
        } else {
            snprintf(lastLine, sizeof lastLine,
                "           last cc %d=%d ch%d\n",
                mi.lastCcNum, mi.lastCcVal, mi.lastCcCh);
        }
    }
    char b[2048];
    snprintf(b, sizeof b,
        "BPM      : %.1f   div: %s\n"
        "sync     : %s\n"
        "%s\n"
        "%s"
        "\n"
        "%-10s tap tempo (inert when MIDI clock is live)\n"
        "%-10s sync on/off\n"
        "%-10s cycle division\n"
        "\n"
        "-- beat-locked modulations --\n"
        "%-10s inject-on-beat     : %s\n"
        "%-10s strobe-rate lock   : %s\n"
        "%-10s vfx auto-cycle     : %s\n"
        "%-10s fade-flash         : %s\n"
        "%-10s decay-dip          : %s\n"
        "\n"
        "beat phase: %.2f",
        p.bpm, BPM_DIV_NAMES[p.divIdx],
        p.bpmSyncOn ? "ON" : "off",
        midiLine,
        lastLine,
        keys_for(ACT_BPM_TAP).c_str(),
        keys_for(ACT_BPM_SYNC_TOGGLE).c_str(),
        keys_for(ACT_BPM_DIV_CYCLE).c_str(),
        keys_for(ACT_BPM_INJECT_TOGGLE).c_str(),   p.bpmInject    ? "ON" : "off",
        keys_for(ACT_BPM_STROBE_TOGGLE).c_str(),   p.bpmStrobe    ? "ON" : "off",
        keys_for(ACT_BPM_VFXCYCLE_TOGGLE).c_str(), p.bpmVfxCycle  ? "ON" : "off",
        keys_for(ACT_BPM_FLASH_TOGGLE).c_str(),    p.bpmFlash     ? "ON" : "off",
        keys_for(ACT_BPM_DECAYDIP_TOGGLE).c_str(), p.bpmDecayDip  ? "ON" : "off",
        p.beatPhase);
    return b;
}

static std::string section_bindings() {
    std::string s;
    s += "All current keyboard bindings.\n";
    s += "Edit bindings.ini next to the exe to customize.\n\n";
    for (const Binding& b : g_input.bindings()) {
        if (b.source != SRC_KEY) continue;
        const ActionInfo* info = action_info(b.action);
        if (!info) continue;
        char row[160];
        snprintf(row, sizeof row, "%-10s %-22s %s\n",
                 fmt_key(b.code, b.modmask).c_str(),
                 info->name, info->desc);
        s += row;
    }
    return s;
}

// ── legend text per section ─────────────────────────────────────────────
// Each section's gamepad map hints to the player what's live. Kept
// human-curated rather than auto-generated from bindings because the
// layout information ("LS translate, RS rotate/zoom, LT/RT fast zoom")
// reads better than a raw list of binding entries.
static std::string legend_for_section(int i) {
    switch (i) {
        case 2:  // Warp
            return "LS translate   RS-X rotate   RS-Y zoom\n"
                   "LT/RT fast zoom (-/+)";
        case 3:  // Optics
            return "LS blur X/Y   RS-X blur angle   RS-Y chroma\n"
                   "LB/RB cycle blur/CA quality";
        case 4:  // Color
            return "LS hue / sat   RS-X contrast   RS-Y gamma\n"
                   "LT/RT hue slew (-/+)";
        case 5:  // Dynamics
            return "LS-X couple   LS-Y ext-cam   RS-X noise   RS-Y decay\n"
                   "LT decay dip     RT noise boost";
        case 6:  // Physics
            return "LS-X sensor-gamma   LS-Y sat-knee   RS-X color-cross\n"
                   "Y invert toggle   LT/RT sensor-gamma fast (-/+)";
        case 7:  // Thermal
            return "LS-X scale   LS-Y amp   RS-X speed   RS-Y rise\n"
                   "LT/RT swirl (-/+)";
        case 8:  // Inject
            return "DP-L/R cursor   A tap-inject   LT/RT hold\n"
                   "X H-bars   Y dot   LB checker   RB gradient";
        case 9:  // VFX-1
            return "DP-L/R or LB/RB cycle   LT/RT param (-/+)\n"
                   "LS-X param   X off   Y B-source";
        case 10: // VFX-2
            return "DP-L/R or LB/RB cycle   LT/RT param (-/+)\n"
                   "LS-X param   X off   Y B-source";
        case 11: // Output
            return "RS-Y fade (absolute, self-centering)\n"
                   "LS-Y fade (rate)   LB fade- / RB fade+";
        case 12: // BPM
            return "A tap   Y sync   X div   Start vfx-cycle\n"
                   "LB inject-on-beat   RB strobe-lock\n"
                   "LSC flash   RSC decay-dip";
        case 13: // Quality
            return "DP-L/R cursor   A cycle armed\n"
                   "LB blur   RB CA   X noise   Y fields";
        case 14: // App
            return "X clear   Y pause   LB/RB preset prev/next\n"
                   "Start save preset   LSC record   RSC fullscreen";
        case 1:  // Layers
            return "DP-L/R  cursor   A  toggle armed\n"
                   "X warp   Y optics   LB ext   RB inject\n"
                   "LSC couple   RSC physics   Start thermal";
        default:
            // Status / Bindings: no contextual gamepad.
            return "";
    }
}

// Master dispatcher — maps overlay section index to the builder above.
static std::string help_section_body(int i) {
    switch (i) {
        case 0:  return section_status();
        case 1:  return section_layers();
        case 2:  return section_warp();
        case 3:  return section_optics();
        case 4:  return section_color();
        case 5:  return section_dynamics();
        case 6:  return section_physics();
        case 7:  return section_thermal();
        case 8:  return section_inject();
        case 9:  return section_vfx(0);
        case 10: return section_vfx(1);
        case 11: return section_output();
        case 12: return section_bpm();
        case 13: return section_quality();
        case 14: return section_app();
        case 15: return section_bindings();
    }
    return "";
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

// ─────────────────────────────────────────────────────────────────────────
// BPM — tap tempo + beat-clock + modulation dispatch.
//
// Each tap appends a timestamp; bpm is recomputed as the median of up to
// 4 recent intervals. A gap >2s resets the tap buffer.
//
// Per frame, beatPhase advances; on phase wrap (crossing from ≈1 back to
// 0) we fire one "beat event" and dispatch any enabled modulations.
// ─────────────────────────────────────────────────────────────────────────
static std::vector<double> g_bpmTaps;

static void bpm_tap(double now) {
    // If MIDI Clock is driving tempo, tap-tempo goes inert — announce
    // rather than silently do nothing.
    if (g_input.midi().clockLive) {
        S.ov.logEvent("BPM: MIDI clock locked (tap ignored)");
        return;
    }
    // Reset tap history after a big gap.
    if (!g_bpmTaps.empty() && (now - g_bpmTaps.back()) > 2.0)
        g_bpmTaps.clear();
    g_bpmTaps.push_back(now);
    if (g_bpmTaps.size() > 5) g_bpmTaps.erase(g_bpmTaps.begin());

    if (g_bpmTaps.size() >= 2) {
        // Median of intervals is more robust than mean against a bad tap.
        std::vector<double> intervals;
        for (size_t i = 1; i < g_bpmTaps.size(); i++)
            intervals.push_back(g_bpmTaps[i] - g_bpmTaps[i-1]);
        std::sort(intervals.begin(), intervals.end());
        double median = intervals[intervals.size() / 2];
        if (median > 0.05 && median < 3.0) {
            float bpm = (float)(60.0 / median);
            if (bpm < 30.f) bpm = 30.f;
            if (bpm > 300.f) bpm = 300.f;
            S.p.bpm = bpm;
            // Re-anchor the beat origin to the latest tap so the phase
            // aligns with the user's intention.
            S.p.beatOrigin = now;
            char b[64]; snprintf(b, sizeof b, "BPM: %.1f", bpm);
            S.ov.logEvent(b);
        }
    } else {
        S.ov.logEvent("BPM: tap again");
    }
}

// Called once per displayed frame. dt is the frame delta time. Advances
// the beat phase, detects beat crossings, and fires the enabled
// modulations for each crossing.
static void update_bpm(double now, float dt) {
    (void)dt;
    auto& p = S.p;

    // Decay transient fade-flash toward 0 each frame — ~150ms half-life.
    if (p.flashDecay != 0.0f) {
        p.flashDecay *= 0.80f;
        if (std::fabs(p.flashDecay) < 0.01f) p.flashDecay = 0.0f;
    }
    // Decay dip timer.
    if (p.decayDipTimer > 0.0f) {
        p.decayDipTimer -= dt;
        if (p.decayDipTimer < 0.0f) p.decayDipTimer = 0.0f;
    }

    // ── MIDI Clock override ──────────────────────────────────────────────
    // If Strudel (or any upstream) is streaming MIDI Clock, its tempo wins
    // over both the manual `p.bpm` value and tap-tempo. Start (0xFA)
    // re-anchors our beat phase; Stop (0xFC) freezes beat events until the
    // clock resumes.
    auto& midi = g_input.midi();
    static bool s_midiFrozen = false;
    if (midi.clockLive && midi.derivedBpm > 0.0f) {
        p.bpm = midi.derivedBpm;
        if (!p.bpmSyncOn) {
            p.bpmSyncOn   = true;         // auto-on when clock is driving
            p.beatOrigin  = now;
        }
        s_midiFrozen = false;
    }
    if (midi.startPending) {
        p.beatOrigin  = now;
        p.bpmSyncOn   = true;
        p.beatPhase   = 0.0f;
        s_midiFrozen  = false;
        midi.startPending = false;
        S.ov.logEvent("MIDI Start");
    }
    if (midi.stopPending) {
        s_midiFrozen = true;
        midi.stopPending = false;
        S.ov.logEvent("MIDI Stop");
    }

    // Beat period (seconds) for the current division.
    float basePeriod = 60.0f / fmaxf(p.bpm, 1.0f);
    float period     = basePeriod * BPM_DIV_MUL[p.divIdx];

    // Fractional position through the current beat.
    double since = now - p.beatOrigin;
    double beats = since / period;
    float  phase = (float)(beats - std::floor(beats));
    float  prev  = p.beatPhase;
    p.beatPhase  = phase;

    // Crossing: phase jumped backwards (wrapped from near-1 to near-0).
    // Also fire on first frame (prev==0 and phase just seeded) — skip
    // the very first frame of the loop by requiring a real crossing.
    bool crossed = (prev > 0.5f && phase < 0.5f);
    if (!p.bpmSyncOn || s_midiFrozen || !crossed) return;

    // ── beat event ── fire enabled modulations.
    if (p.bpmInject) {
        p.inject = 1.0f;   // same shape as ACT_INJECT_HOLD press.
    }
    if (p.bpmVfxCycle) {
        // Advance slot 1's effect, skipping 'off'.
        p.vfxSlot[0] = (p.vfxSlot[0] % (VFX_COUNT - 1)) + 1;
    }
    if (p.bpmFlash) {
        // Alternate between white and black flashes per beat.
        static int tick = 0;
        tick++;
        p.flashDecay = (tick & 1) ? +0.6f : -0.6f;
    }
    if (p.bpmDecayDip) {
        p.decayDipTimer = 0.08f;   // ~5 frames at 60fps
    }
}

// ─────────────────────────────────────────────────────────────────────────
// apply_action — single-source-of-truth mutator for every ActionId.
//
// All input sources (keyboard, gamepad C2, MIDI future) dispatch through
// this function via the g_input callback. `mag` semantics:
//   STEP  actions: signed step count including Shift's 20x multiplier,
//                  i.e. +1.0 normal, +20.0 with Shift, -1.0 with invert.
//                  apply_action scales by the per-parameter fine step.
//   DISCRETE:      1.0 on fire.
//   TRIGGER:       1.0 on press, 0.0 on release.
//   RATE:          magnitude already integrated by the caller (C2 gamepad).
//
// Per-parameter step sizes preserved byte-for-byte from pre-refactor code.
// ─────────────────────────────────────────────────────────────────────────

// hud(): post one parameter change to the overlay with cumulative-burst
// aggregation. Lifted out of the old key_cb so every action dispatch goes
// through the same formatter regardless of source.
static void hud_post(const char* key, const char* label,
                     float rawDelta, float rawValue,
                     float scaleD, const char* unitD,
                     float scaleV, const char* unitV)
{
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
}

// Per-parameter fine step sizes, tuned for 60fps feedback dynamics. These
// are multiplied by the incoming magnitude (which already includes Shift
// coarse multiplier and any binding-level scale), so a Shift+Q becomes
// `+20 * 0.0002 = +0.004` zoom units per press.
namespace step {
    constexpr float zoom    = 0.0002f;  // 0.02% per pass
    constexpr float theta   = 0.0002f;  // ~0.01 deg per pass
    constexpr float trans   = 0.0005f;
    constexpr float chroma  = 0.0002f;
    constexpr float blurR   = 0.02f;
    constexpr float blurA   = 0.005f;
    constexpr float gammaS  = 0.01f;
    constexpr float hue     = 0.0002f;
    constexpr float sat     = 0.002f;
    constexpr float contrast= 0.002f;
    constexpr float decay   = 0.0005f;
    constexpr float noise   = 0.0002f;
    constexpr float couple  = 0.002f;
    constexpr float ext     = 0.002f;
    constexpr float sensorG = 0.005f;
    constexpr float knee    = 0.005f;
    constexpr float cross   = 0.002f;
    constexpr float thermAmp   = 0.002f;
    constexpr float thermScale = 0.2f;
    constexpr float thermSpeed = 0.1f;
    constexpr float thermRise  = 0.02f;
    constexpr float thermSwirl = 0.02f;
}

static void apply_action(ActionId id, float mag) {
    auto& p = S.p;

    // When the help panel is open, UP/DOWN/ENTER/ESC are hijacked for menu
    // navigation. They stay bound to their normal actions (translate, quit)
    // so the binding table doesn't need a mode-aware layer; we just
    // intercept those few conflicting ids here.
    if (S.ov.helpVisible()) {
        switch (id) {
            case ACT_HELP_UP:    S.ov.helpUp();    return;
            case ACT_HELP_DN:    S.ov.helpDown();  return;
            case ACT_HELP_ENTER: S.ov.helpEnter(); return;
            case ACT_HELP_BACK:  S.ov.helpBack();  return;
            // Suppress conflicting shared bindings while help has focus.
            case ACT_TRANS_UP: case ACT_TRANS_DN:
            case ACT_TRANS_LEFT: case ACT_TRANS_RIGHT:
            case ACT_QUIT:
                return;
            default: break;  // everything else keeps working during navigation
        }
    } else {
        // Help not visible — help-nav actions are no-ops (Enter/Esc wired
        // elsewhere if needed).
        switch (id) {
            case ACT_HELP_UP: case ACT_HELP_DN:
            case ACT_HELP_ENTER: case ACT_HELP_BACK:
                // Esc still quits via ACT_QUIT (a separate binding). Enter
                // has no default action outside help.
                return;
            default: break;
        }
    }

    // Toggle layers via a small table lookup — replaces the old in-cb loop.
    auto toggle_layer = [&](int bit, const char* name) {
        S.enable ^= bit;
        bool on = (S.enable & bit) != 0;
        printf("[%s] %s\n", name, on ? "ON" : "off");
        char buf[64]; snprintf(buf, sizeof buf, "%s: %s", name, on ? "ON" : "off");
        S.ov.logEvent(buf);
    };

    switch (id) {
        // ── layer toggles ─────────────────────────────────────────
        case ACT_LAYER_WARP:     toggle_layer(L_WARP,     "warp");     return;
        case ACT_LAYER_OPTICS:   toggle_layer(L_OPTICS,   "optics");   return;
        case ACT_LAYER_GAMMA:    toggle_layer(L_GAMMA,    "gamma");    return;
        case ACT_LAYER_COLOR:    toggle_layer(L_COLOR,    "color");    return;
        case ACT_LAYER_CONTRAST: toggle_layer(L_CONTRAST, "contrast"); return;
        case ACT_LAYER_DECAY:    toggle_layer(L_DECAY,    "decay");    return;
        case ACT_LAYER_NOISE:    toggle_layer(L_NOISE,    "noise");    return;
        case ACT_LAYER_COUPLE:   toggle_layer(L_COUPLE,   "couple");   return;
        case ACT_LAYER_EXTERNAL: toggle_layer(L_EXTERNAL, "external"); return;
        case ACT_LAYER_INJECT:   toggle_layer(L_INJECT,   "inject");   return;
        case ACT_LAYER_PHYSICS:  toggle_layer(L_PHYSICS,  "physics");  return;
        case ACT_LAYER_THERMAL:  toggle_layer(L_THERMAL,  "thermal");  return;

        // Layer cursor — gamepad-driven navigation across the 12 layers.
        // Wraps at both ends. Pairs with ACT_LAYER_TOGGLE_ARMED (A).
        case ACT_LAYER_CURSOR_UP: {
            const int N = sizeof(LAYERS) / sizeof(LAYERS[0]);
            S.armedLayer = (S.armedLayer - 1 + N) % N;
            char b[64]; snprintf(b, sizeof b, "layer armed: %s", LAYERS[S.armedLayer].name);
            S.ov.logEvent(b); return;
        }
        case ACT_LAYER_CURSOR_DN: {
            const int N = sizeof(LAYERS) / sizeof(LAYERS[0]);
            S.armedLayer = (S.armedLayer + 1) % N;
            char b[64]; snprintf(b, sizeof b, "layer armed: %s", LAYERS[S.armedLayer].name);
            S.ov.logEvent(b); return;
        }
        case ACT_LAYER_TOGGLE_ARMED: {
            const LayerInfo& li = LAYERS[S.armedLayer];
            toggle_layer(li.bit, li.name);
            return;
        }

        // Quality cursor — 4 entries: blur, ca, noise, fields.
        case ACT_QUALITY_CURSOR_UP:
            S.armedQuality = (S.armedQuality - 1 + 4) % 4;
            { static const char* N[] = {"blur","CA","noise","fields"};
              char b[64]; snprintf(b, sizeof b, "quality armed: %s", N[S.armedQuality]);
              S.ov.logEvent(b); }
            return;
        case ACT_QUALITY_CURSOR_DN:
            S.armedQuality = (S.armedQuality + 1) % 4;
            { static const char* N[] = {"blur","CA","noise","fields"};
              char b[64]; snprintf(b, sizeof b, "quality armed: %s", N[S.armedQuality]);
              S.ov.logEvent(b); }
            return;
        case ACT_QUALITY_FIRE_ARMED:
            switch (S.armedQuality) {
                case 0: apply_action(ACT_BLURQ_CYCLE,  1.0f); break;
                case 1: apply_action(ACT_CAQ_CYCLE,    1.0f); break;
                case 2: apply_action(ACT_NOISEQ_CYCLE, 1.0f); break;
                case 3: apply_action(ACT_FIELDS_CYCLE, 1.0f); break;
            }
            return;

        // Pattern cursor — 5 entries. Modifies p.pattern directly; the
        // Inject section's body highlights whichever p.pattern points at.
        case ACT_PATTERN_CURSOR_UP:
            p.pattern = (p.pattern - 1 + 5) % 5;
            { static const char* N[] = {"H-bars","V-bars","dot","checker","gradient"};
              char b[64]; snprintf(b, sizeof b, "pattern: %s", N[p.pattern]);
              S.ov.logEvent(b); }
            return;
        case ACT_PATTERN_CURSOR_DN:
            p.pattern = (p.pattern + 1) % 5;
            { static const char* N[] = {"H-bars","V-bars","dot","checker","gradient"};
              char b[64]; snprintf(b, sizeof b, "pattern: %s", N[p.pattern]);
              S.ov.logEvent(b); }
            return;

        // ── warp ──────────────────────────────────────────────────
        case ACT_ZOOM_UP:   { float d = step::zoom * mag; p.zoom += d;
            hud_post("zoom","zoom", d, p.zoom-1.0f, 100.f,"% /pass", 100.f,"% /pass"); break; }
        case ACT_ZOOM_DN:   { float d = -step::zoom * mag; p.zoom += d;
            hud_post("zoom","zoom", d, p.zoom-1.0f, 100.f,"% /pass", 100.f,"% /pass"); break; }
        case ACT_THETA_UP:  { float d = step::theta * mag; p.theta += d;
            hud_post("theta","rotation", d, p.theta, 57.2958f,"deg/pass", 57.2958f*60.f,"deg/s"); break; }
        case ACT_THETA_DN:  { float d = -step::theta * mag; p.theta += d;
            hud_post("theta","rotation", d, p.theta, 57.2958f,"deg/pass", 57.2958f*60.f,"deg/s"); break; }
        case ACT_TRANS_LEFT:  { float d = -step::trans * mag; p.transX += d;
            hud_post("trX","trans-X", d, p.transX, 100.f,"% /pass", 100.f,"% of frame"); break; }
        case ACT_TRANS_RIGHT: { float d =  step::trans * mag; p.transX += d;
            hud_post("trX","trans-X", d, p.transX, 100.f,"% /pass", 100.f,"% of frame"); break; }
        case ACT_TRANS_UP:    { float d = -step::trans * mag; p.transY += d;
            hud_post("trY","trans-Y", d, p.transY, 100.f,"% /pass", 100.f,"% of frame"); break; }
        case ACT_TRANS_DN:    { float d =  step::trans * mag; p.transY += d;
            hud_post("trY","trans-Y", d, p.transY, 100.f,"% /pass", 100.f,"% of frame"); break; }

        // ── optics ────────────────────────────────────────────────
        case ACT_CHROMA_UP: { float d =  step::chroma * mag; p.chroma += d;
            hud_post("chr","chroma", d, p.chroma, 1000.f,"milli", 1000.f,"milli"); break; }
        case ACT_CHROMA_DN: { float d = -step::chroma * mag; p.chroma += d;
            hud_post("chr","chroma", d, p.chroma, 1000.f,"milli", 1000.f,"milli"); break; }
        case ACT_BLURX_UP:  { float d =  step::blurR * mag; p.blurX += d;
            hud_post("blrX","blur-X", d, p.blurX, 1.f,"px", 1.f,"px"); break; }
        case ACT_BLURX_DN:  { float d = -step::blurR * mag; p.blurX += d;
            hud_post("blrX","blur-X", d, p.blurX, 1.f,"px", 1.f,"px"); break; }
        case ACT_BLURY_UP:  { float d =  step::blurR * mag; p.blurY += d;
            hud_post("blrY","blur-Y", d, p.blurY, 1.f,"px", 1.f,"px"); break; }
        case ACT_BLURY_DN:  { float d = -step::blurR * mag; p.blurY += d;
            hud_post("blrY","blur-Y", d, p.blurY, 1.f,"px", 1.f,"px"); break; }
        case ACT_BLURANG_UP:{ float d =  step::blurA * mag; p.blurAngle += d;
            hud_post("blrA","blur-ang", d, p.blurAngle, 57.2958f,"deg", 57.2958f,"deg"); break; }
        case ACT_BLURANG_DN:{ float d = -step::blurA * mag; p.blurAngle += d;
            hud_post("blrA","blur-ang", d, p.blurAngle, 57.2958f,"deg", 57.2958f,"deg"); break; }

        // ── color / tone ──────────────────────────────────────────
        case ACT_GAMMA_UP: { float d =  step::gammaS * mag; p.gamma += d;
            hud_post("gam","gamma", d, p.gamma, 1.f,"", 1.f,""); break; }
        case ACT_GAMMA_DN: { float d = -step::gammaS * mag;
            p.gamma = fmaxf(0.1f, p.gamma + d);
            hud_post("gam","gamma", d, p.gamma, 1.f,"", 1.f,""); break; }
        case ACT_HUE_UP:   { float d =  step::hue * mag; p.hueRate += d;
            hud_post("hue","hue rate", d, p.hueRate, 1000.f,"milli/pass", 60.f,"rot/s"); break; }
        case ACT_HUE_DN:   { float d = -step::hue * mag; p.hueRate += d;
            hud_post("hue","hue rate", d, p.hueRate, 1000.f,"milli/pass", 60.f,"rot/s"); break; }
        case ACT_SAT_UP:   { float d =  step::sat * mag; p.satGain += d;
            hud_post("sat","satur", d, p.satGain, 100.f,"%", 1.f,"x"); break; }
        case ACT_SAT_DN:   { float d = -step::sat * mag; p.satGain += d;
            hud_post("sat","satur", d, p.satGain, 100.f,"%", 1.f,"x"); break; }
        case ACT_CONTRAST_UP: { float d =  step::contrast * mag; p.contrast += d;
            hud_post("con","contrast", d, p.contrast, 100.f,"%", 1.f,"x"); break; }
        case ACT_CONTRAST_DN: { float d = -step::contrast * mag; p.contrast += d;
            hud_post("con","contrast", d, p.contrast, 100.f,"%", 1.f,"x"); break; }

        // ── dynamics ──────────────────────────────────────────────
        case ACT_DECAY_UP: { float d =  step::decay * mag;
            p.decay = fminf(1.0f, p.decay + d);
            hud_post("dec","decay", d, p.decay, 1000.f,"milli", 1000.f,"milli"); break; }
        case ACT_DECAY_DN: { float d = -step::decay * mag;
            p.decay = fmaxf(0.9f, p.decay + d);
            hud_post("dec","decay", d, p.decay, 1000.f,"milli", 1000.f,"milli"); break; }
        case ACT_NOISE_UP: { float d =  step::noise * mag; p.noise += d;
            hud_post("noi","noise", d, p.noise, 1000.f,"milli", 1000.f,"milli"); break; }
        case ACT_NOISE_DN: { float d = -step::noise * mag;
            p.noise = fmaxf(0.0f, p.noise + d);
            hud_post("noi","noise", d, p.noise, 1000.f,"milli", 1000.f,"milli"); break; }
        case ACT_COUPLE_UP: { float d =  step::couple * mag;
            p.couple = fminf(1.0f, p.couple + d);
            hud_post("cpl","couple", d, p.couple, 100.f,"%", 100.f,"%"); break; }
        case ACT_COUPLE_DN: { float d = -step::couple * mag;
            p.couple = fmaxf(0.0f, p.couple + d);
            hud_post("cpl","couple", d, p.couple, 100.f,"%", 100.f,"%"); break; }
        case ACT_EXTERNAL_UP: { float d =  step::ext * mag;
            p.external = fminf(1.0f, p.external + d);
            hud_post("ext","external", d, p.external, 100.f,"%", 100.f,"%"); break; }
        case ACT_EXTERNAL_DN: { float d = -step::ext * mag;
            p.external = fmaxf(0.0f, p.external + d);
            hud_post("ext","external", d, p.external, 100.f,"%", 100.f,"%"); break; }

        // ── physics (Crutchfield) ────────────────────────────────
        case ACT_INVERT_TOGGLE: {
            p.invert = 1 - p.invert;
            printf("[invert] %s\n", p.invert ? "ON" : "off");
            char b[64]; snprintf(b, sizeof b, "luminance invert: %s",
                                 p.invert ? "ON" : "off");
            S.ov.logEvent(b); return;
        }
        case ACT_SENSORGAMMA_UP: { float d =  step::sensorG * mag;
            p.sensorGamma = fminf(2.0f, p.sensorGamma + d);
            hud_post("sgam","sens-gamma", d, p.sensorGamma, 1.f,"", 1.f,""); break; }
        case ACT_SENSORGAMMA_DN: { float d = -step::sensorG * mag;
            p.sensorGamma = fmaxf(0.1f, p.sensorGamma + d);
            hud_post("sgam","sens-gamma", d, p.sensorGamma, 1.f,"", 1.f,""); break; }
        case ACT_SATKNEE_UP: { float d =  step::knee * mag;
            p.satKnee = fminf(1.0f, p.satKnee + d);
            hud_post("knee","sat-knee", d, p.satKnee, 100.f,"%", 100.f,"%"); break; }
        case ACT_SATKNEE_DN: { float d = -step::knee * mag;
            p.satKnee = fmaxf(0.0f, p.satKnee + d);
            hud_post("knee","sat-knee", d, p.satKnee, 100.f,"%", 100.f,"%"); break; }
        case ACT_COLORCROSS_UP: { float d =  step::cross * mag;
            p.colorCross = fminf(1.0f, p.colorCross + d);
            hud_post("cx","color-xtalk", d, p.colorCross, 100.f,"%", 100.f,"%"); break; }
        case ACT_COLORCROSS_DN: { float d = -step::cross * mag;
            p.colorCross = fmaxf(0.0f, p.colorCross + d);
            hud_post("cx","color-xtalk", d, p.colorCross, 100.f,"%", 100.f,"%"); break; }

        // ── thermal ───────────────────────────────────────────────
        case ACT_THERMAMP_UP: { float d =  step::thermAmp * mag;
            p.thermAmp = fminf(1.0f, p.thermAmp + d);
            hud_post("tAmp","therm-amp", d, p.thermAmp, 100.f,"%", 100.f,"%"); break; }
        case ACT_THERMAMP_DN: { float d = -step::thermAmp * mag;
            p.thermAmp = fmaxf(0.0f, p.thermAmp + d);
            hud_post("tAmp","therm-amp", d, p.thermAmp, 100.f,"%", 100.f,"%"); break; }
        case ACT_THERMSCALE_UP: { float d =  step::thermScale * mag;
            p.thermScale = fminf(40.0f, p.thermScale + d);
            hud_post("tSca","therm-scale", d, p.thermScale, 1.f,"", 1.f,""); break; }
        case ACT_THERMSCALE_DN: { float d = -step::thermScale * mag;
            p.thermScale = fmaxf(0.2f, p.thermScale + d);
            hud_post("tSca","therm-scale", d, p.thermScale, 1.f,"", 1.f,""); break; }
        case ACT_THERMSPEED_UP: { float d =  step::thermSpeed * mag;
            p.thermSpeed = fminf(30.0f, p.thermSpeed + d);
            hud_post("tSpd","therm-speed", d, p.thermSpeed, 1.f,"", 1.f,""); break; }
        case ACT_THERMSPEED_DN: { float d = -step::thermSpeed * mag;
            p.thermSpeed = fmaxf(0.0f, p.thermSpeed + d);
            hud_post("tSpd","therm-speed", d, p.thermSpeed, 1.f,"", 1.f,""); break; }
        case ACT_THERMRISE_UP: { float d =  step::thermRise * mag;
            p.thermRise = fminf(2.0f, p.thermRise + d);
            hud_post("tRis","therm-rise", d, p.thermRise, 1.f,"", 1.f,""); break; }
        case ACT_THERMRISE_DN: { float d = -step::thermRise * mag;
            p.thermRise = fmaxf(-1.0f, p.thermRise + d);
            hud_post("tRis","therm-rise", d, p.thermRise, 1.f,"", 1.f,""); break; }
        case ACT_THERMSWIRL_UP: { float d =  step::thermSwirl * mag;
            p.thermSwirl = fminf(2.0f, p.thermSwirl + d);
            hud_post("tSwi","therm-swirl", d, p.thermSwirl, 1.f,"", 1.f,""); break; }
        case ACT_THERMSWIRL_DN: { float d = -step::thermSwirl * mag;
            p.thermSwirl = fmaxf(0.0f, p.thermSwirl + d);
            hud_post("tSwi","therm-swirl", d, p.thermSwirl, 1.f,"", 1.f,""); break; }

        // ── patterns / inject ─────────────────────────────────────
        case ACT_PATTERN_HBARS:   p.pattern = 0; printf("[pattern] H-bars\n");
            S.ov.logEvent("pattern: H-bars"); return;
        case ACT_PATTERN_VBARS:   p.pattern = 1; printf("[pattern] V-bars\n");
            S.ov.logEvent("pattern: V-bars"); return;
        case ACT_PATTERN_DOT:     p.pattern = 2; printf("[pattern] dot\n");
            S.ov.logEvent("pattern: dot"); return;
        case ACT_PATTERN_CHECKER: p.pattern = 3; printf("[pattern] checker\n");
            S.ov.logEvent("pattern: checker"); return;
        case ACT_PATTERN_GRAD:    p.pattern = 4; printf("[pattern] gradient\n");
            S.ov.logEvent("pattern: gradient"); return;
        case ACT_INJECT_HOLD: {
            if (mag > 0.5f) {
                p.inject = 1.0f;
                static const char* names[] = {"H-bars","V-bars","dot","checker","gradient"};
                printf("[inject pattern=%d]\n", p.pattern);
                char buf[64]; snprintf(buf, sizeof buf, "inject: %s", names[p.pattern]);
                S.ov.logEvent(buf);
            } else {
                p.inject = 0.3f;
            }
            return;
        }

        // ── app ───────────────────────────────────────────────────
        case ACT_QUIT: glfwSetWindowShouldClose(S.win, 1); return;
        case ACT_CLEAR: S.needClear = true; printf("[cleared]\n");
            S.ov.logEvent("cleared"); return;
        case ACT_PAUSE: S.paused = !S.paused;
            printf("[%s]\n", S.paused ? "paused" : "running");
            S.ov.logEvent(S.paused ? "paused" : "running"); return;
        case ACT_HELP: S.ov.toggleHelp();
            printf("[help] %s\n", S.ov.helpVisible() ? "shown" : "hidden"); return;
        case ACT_RELOAD_SHADERS: reload_shaders();
            S.ov.logEvent("shaders reloaded"); return;
        case ACT_FULLSCREEN: {
            toggle_fullscreen();
            char b[64]; snprintf(b, sizeof b, "fullscreen: %s",
                                 S.isFullscreen ? "ON" : "off");
            S.ov.logEvent(b); return;
        }
        case ACT_REC_TOGGLE:
            if (S.rec.active()) {
                S.rec.stop();
                if (!S.rec.lastDir().empty())
                    S.recordingsThisSession.push_back(S.rec.lastDir());
                S.ov.logEvent("recording: stopped");
            } else {
                Recorder::Config rcfg{};
                rcfg.ramBudgetMB   = g_cfg.recRamMB;
                rcfg.encoderThreads = g_cfg.recEncoders;
                rcfg.uncompressed  = g_cfg.recUncompressed;
                S.rec.start(S.simW, S.simH, g_cfg.recFps, g_cfg.precision,
                            S.win, rcfg);
                S.ov.logEvent("recording: started");
            }
            return;
        case ACT_SCREENSHOT:
            S.screenshotPending = true;
            S.ov.logEvent("screenshot queued");
            return;
        case ACT_PRESET_SAVE: {
            std::string fn = preset_save_now();
            if (!fn.empty()) {
                printf("[preset] saved %s\n", fn.c_str());
                char b[128]; snprintf(b, sizeof b, "preset SAVED: %s", fn.c_str());
                S.ov.logEvent(b);
            } else S.ov.logEvent("preset save FAILED");
            return;
        }
        case ACT_PRESET_NEXT: {
            std::string n = preset_cycle(+1);
            if (!n.empty()) {
                printf("[preset] loaded %s  (%d/%zu)\n", n.c_str(),
                       g_currentPreset+1, g_presetFiles.size());
                char b[128]; snprintf(b, sizeof b, "preset: %s  (%d/%zu)",
                                      n.c_str(), g_currentPreset+1, g_presetFiles.size());
                S.ov.logEvent(b);
            } else S.ov.logEvent("no presets in ./presets/");
            return;
        }
        case ACT_PRESET_PREV: {
            std::string n = preset_cycle(-1);
            if (!n.empty()) {
                printf("[preset] loaded %s  (%d/%zu)\n", n.c_str(),
                       g_currentPreset+1, g_presetFiles.size());
                char b[128]; snprintf(b, sizeof b, "preset: %s  (%d/%zu)",
                                      n.c_str(), g_currentPreset+1, g_presetFiles.size());
                S.ov.logEvent(b);
            } else S.ov.logEvent("no presets in ./presets/");
            return;
        }
        case ACT_BLURQ_CYCLE: {
            S.blurQ = (S.blurQ + 1) % 3;
            const char* n[] = {"5-tap cross","9-tap gauss","25-tap gauss"};
            printf("[blur-q] %s\n", n[S.blurQ]);
            char b[64]; snprintf(b, sizeof b, "blur: %s", n[S.blurQ]);
            S.ov.logEvent(b); return;
        }
        case ACT_CAQ_CYCLE: {
            S.caQ = (S.caQ + 1) % 3;
            const char* n[] = {"3-sample","5-ramp","8-wavelen"};
            printf("[ca-q] %s\n", n[S.caQ]);
            char b[64]; snprintf(b, sizeof b, "CA: %s", n[S.caQ]);
            S.ov.logEvent(b); return;
        }
        case ACT_NOISEQ_CYCLE: {
            S.noiseQ = (S.noiseQ + 1) % 2;
            const char* n[] = {"white","pink 1/f"};
            printf("[noise-q] %s\n", n[S.noiseQ]);
            char b[64]; snprintf(b, sizeof b, "noise: %s", n[S.noiseQ]);
            S.ov.logEvent(b); return;
        }
        case ACT_FIELDS_CYCLE: {
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
        case ACT_PRINT_HELP_STDOUT: print_help(); return;

        // ── bipolar axes (RATE; mag already scaled per frame) ─────
        case ACT_ZOOM_AXIS:    p.zoom   += step::zoom  * mag; return;
        case ACT_THETA_AXIS:   p.theta  += step::theta * mag; return;
        case ACT_TRANS_X_AXIS: p.transX += step::trans * mag; return;
        case ACT_TRANS_Y_AXIS: p.transY += step::trans * mag; return;
        case ACT_HUE_AXIS:     p.hueRate+= step::hue   * mag; return;
        case ACT_DECAY_AXIS:
            p.decay = fmaxf(0.9f, fminf(1.0f, p.decay + step::decay * mag));
            return;

        // Help-nav actions are intercepted at the top of apply_action;
        // if we reach here it's because help was closed — no-op.
        case ACT_HELP_UP: case ACT_HELP_DN:
        case ACT_HELP_ENTER: case ACT_HELP_BACK:
            return;

        // ── V-4 effect slots ───────────────────────────────────────
        #define VFX_LOG_SLOT(slot) do { \
            int e = p.vfxSlot[slot]; \
            char _b[96]; \
            if (e == 0) snprintf(_b, sizeof _b, "vfx%d: off", (slot)+1); \
            else        snprintf(_b, sizeof _b, "vfx%d: %s  p=%.2f  B=%s", \
                                 (slot)+1, VFX_NAMES[e], p.vfxParam[slot], \
                                 VFX_BSRC_NAMES[p.vfxBSource[slot]]); \
            S.ov.logEvent(_b); \
        } while (0)

        case ACT_VFX1_CYCLE_FWD:
            p.vfxSlot[0] = (p.vfxSlot[0] + 1) % VFX_COUNT;
            VFX_LOG_SLOT(0); return;
        case ACT_VFX1_CYCLE_BACK:
            p.vfxSlot[0] = (p.vfxSlot[0] - 1 + VFX_COUNT) % VFX_COUNT;
            VFX_LOG_SLOT(0); return;
        case ACT_VFX1_OFF:
            p.vfxSlot[0] = 0; VFX_LOG_SLOT(0); return;
        case ACT_VFX1_PARAM_UP: {
            p.vfxParam[0] = fminf(1.0f, p.vfxParam[0] + 0.01f * mag);
            hud_post("vfx1p","vfx1 ctrl", 0.01f*mag, p.vfxParam[0], 100.f,"%", 100.f,"%");
            break;
        }
        case ACT_VFX1_PARAM_DN: {
            p.vfxParam[0] = fmaxf(0.0f, p.vfxParam[0] - 0.01f * mag);
            hud_post("vfx1p","vfx1 ctrl", -0.01f*mag, p.vfxParam[0], 100.f,"%", 100.f,"%");
            break;
        }
        case ACT_VFX1_BSRC_CYCLE:
            p.vfxBSource[0] = (p.vfxBSource[0] + 1) % 2; VFX_LOG_SLOT(0); return;

        case ACT_VFX2_CYCLE_FWD:
            p.vfxSlot[1] = (p.vfxSlot[1] + 1) % VFX_COUNT;
            VFX_LOG_SLOT(1); return;
        case ACT_VFX2_CYCLE_BACK:
            p.vfxSlot[1] = (p.vfxSlot[1] - 1 + VFX_COUNT) % VFX_COUNT;
            VFX_LOG_SLOT(1); return;
        case ACT_VFX2_OFF:
            p.vfxSlot[1] = 0; VFX_LOG_SLOT(1); return;
        case ACT_VFX2_PARAM_UP: {
            p.vfxParam[1] = fminf(1.0f, p.vfxParam[1] + 0.01f * mag);
            hud_post("vfx2p","vfx2 ctrl", 0.01f*mag, p.vfxParam[1], 100.f,"%", 100.f,"%");
            break;
        }
        case ACT_VFX2_PARAM_DN: {
            p.vfxParam[1] = fmaxf(0.0f, p.vfxParam[1] - 0.01f * mag);
            hud_post("vfx2p","vfx2 ctrl", -0.01f*mag, p.vfxParam[1], 100.f,"%", 100.f,"%");
            break;
        }
        case ACT_VFX2_BSRC_CYCLE:
            p.vfxBSource[1] = (p.vfxBSource[1] + 1) % 2; VFX_LOG_SLOT(1); return;

        #undef VFX_LOG_SLOT

        // ── output fade ───────────────────────────────────────────
        case ACT_OUTFADE_UP: {
            float d = 0.02f * mag;
            p.outFade = fmaxf(-1.0f, fminf(1.0f, p.outFade + d));
            hud_post("ofa","out-fade", d, p.outFade, 100.f,"%", 100.f,"%");
            break;
        }
        case ACT_OUTFADE_DN: {
            float d = -0.02f * mag;
            p.outFade = fmaxf(-1.0f, fminf(1.0f, p.outFade + d));
            hud_post("ofa","out-fade", d, p.outFade, 100.f,"%", 100.f,"%");
            break;
        }
        case ACT_OUTFADE_AXIS:
            // Absolute axis: stick position is the fade directly. Self-
            // centers when the stick is released.
            p.outFade = fmaxf(-1.0f, fminf(1.0f, mag));
            return;

        // ── BPM sync + tap tempo ──────────────────────────────────
        case ACT_BPM_TAP: bpm_tap(glfwGetTime()); return;
        case ACT_BPM_SYNC_TOGGLE: {
            p.bpmSyncOn = !p.bpmSyncOn;
            if (p.bpmSyncOn) p.beatOrigin = glfwGetTime();
            char b[64]; snprintf(b, sizeof b, "BPM sync: %s  (%.1f bpm, %s)",
                                 p.bpmSyncOn ? "ON" : "off", p.bpm,
                                 BPM_DIV_NAMES[p.divIdx]);
            S.ov.logEvent(b); return;
        }
        case ACT_BPM_DIV_CYCLE: {
            p.divIdx = (p.divIdx + 1) % N_BPM_DIVS;
            char b[64]; snprintf(b, sizeof b, "BPM div: %s", BPM_DIV_NAMES[p.divIdx]);
            S.ov.logEvent(b); return;
        }
        case ACT_BPM_INJECT_TOGGLE:
            p.bpmInject = !p.bpmInject;
            S.ov.logEvent(p.bpmInject ? "bpm inject-on-beat: ON"
                                      : "bpm inject-on-beat: off"); return;
        case ACT_BPM_STROBE_TOGGLE:
            p.bpmStrobe = !p.bpmStrobe;
            S.ov.logEvent(p.bpmStrobe ? "bpm strobe lock: ON"
                                      : "bpm strobe lock: off"); return;
        case ACT_BPM_VFXCYCLE_TOGGLE:
            p.bpmVfxCycle = !p.bpmVfxCycle;
            S.ov.logEvent(p.bpmVfxCycle ? "bpm vfx auto-cycle: ON"
                                        : "bpm vfx auto-cycle: off"); return;
        case ACT_BPM_FLASH_TOGGLE:
            p.bpmFlash = !p.bpmFlash;
            S.ov.logEvent(p.bpmFlash ? "bpm fade-flash: ON"
                                     : "bpm fade-flash: off"); return;
        case ACT_BPM_DECAYDIP_TOGGLE:
            p.bpmDecayDip = !p.bpmDecayDip;
            S.ov.logEvent(p.bpmDecayDip ? "bpm decay-dip: ON"
                                        : "bpm decay-dip: off"); return;

        case ACT_LAUNCH_LOOPMIDI:
            S.ov.logEvent("installing MIDI driver...");
#ifdef _WIN32
            music_ensure_driver();
#endif
            return;

        case ACT_NONE: case ACT__COUNT: return;
    }
    print_status();
}

static void key_cb(GLFWwindow*, int key, int scancode, int action, int mods) {
    // Input routes to apply_action via the handler installed in main().
    // Kept as a thin forward so gamepad/MIDI (C2+) plug in the same way.
    g_input.onKey(key, scancode, action, mods);
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
    // Beat-driven transients are composed here so p.* values stay the
    // user's "set point". Flash adds/subtracts to outFade (clamped); dip
    // lowers decay to 0.90 while the timer is warm.
    float effOutFade = fmaxf(-1.0f, fminf(1.0f, p.outFade + p.flashDecay));
    float effDecay   = (p.decayDipTimer > 0.0f) ? 0.90f : p.decay;
    U1f("uDecay", effDecay);
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

    // V-4 effect slots. Arrays-of-ints can't be set with U1i; use the
    // array-at-index loc form.
    {
        GLint lEff = glGetUniformLocation(progFeedback, "uVfxEffect");
        GLint lPar = glGetUniformLocation(progFeedback, "uVfxParam");
        GLint lSrc = glGetUniformLocation(progFeedback, "uVfxBSource");
        if (lEff >= 0) glUniform1iv(lEff, 2, p.vfxSlot);
        if (lPar >= 0) glUniform1fv(lPar, 2, p.vfxParam);
        if (lSrc >= 0) glUniform1iv(lSrc, 2, p.vfxBSource);
    }
    U1f("uOutFade", effOutFade);

    // BPM strobe-lock uniforms for vfx_slot.glsl.
    U1f("uBpmPhase", p.beatPhase);
    U1i("uBpmStrobeLock", (p.bpmSyncOn && p.bpmStrobe) ? 1 : 0);

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

    // No CLI args → user double-clicked the exe from Explorer. Show a mode
    // picker on the console. Any flag (even --help) skips this path.
    if (argc == 1) run_mode_picker(g_cfg);

    // Passive MIDI hint: if no input port is visible, show a one-time
    // setup pointer so CLI users know Strudel sync is a flip away.
    music_startup_hint();

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
    S.demoPresetSec = g_cfg.demoPresetSec;
    S.demoInjectSec = g_cfg.demoInjectSec;

    const char* blurNames[]  = { "5-tap cross", "9-tap gauss", "25-tap gauss" };
    const char* caNames[]    = { "3-sample", "5-sample ramp", "8-sample wavelen" };
    const char* noiseNames[] = { "white", "pink 1/f" };
    printf("[sim] %dx%d  precision=%s  iters=%d\n",
           S.simW, S.simH, precision_label(g_cfg.precision), g_cfg.iters);
    printf("[quality] blur=%s  ca=%s  noise=%s  fields=%d\n",
           blurNames[S.blurQ], caNames[S.caQ], noiseNames[S.noiseQ], S.activeFields);

    // Input: build default keyboard map, then overlay bindings.ini if present.
    // bindings.ini lives next to the executable (or at CWD) — we write a default
    // on first run so users have something to edit.
    g_input.installDefaults();
    g_input.setHandler(apply_action);

    // Help panel: ordered section list + provider. Each section's body is
    // rebuilt on every frame it's visible so live values stay fresh.
    {
        std::vector<std::string> names;
        names.reserve(N_HELP_SECTIONS);
        for (int i = 0; i < N_HELP_SECTIONS; i++) names.emplace_back(HELP_SECTIONS[i]);
        S.ov.setHelpSections(std::move(names));
        S.ov.setHelpProvider(help_section_body);
        S.ov.setLegendProvider(legend_for_section);
        S.ov.setActiveSection(2);    // default: Warp until the user drills in
    }
    {
        std::string bindingsPath = g_shader_base.empty()
            ? std::string("bindings.ini")
            : (g_shader_base + "bindings.ini");
        if (!g_input.loadIni(bindingsPath)) {
            if (g_input.saveIni(bindingsPath))
                printf("[bindings] wrote default %s\n", bindingsPath.c_str());
        } else {
            printf("[bindings] loaded %s\n", bindingsPath.c_str());
        }
    }

    if (glfwJoystickPresent(GLFW_JOYSTICK_1)) {
        const char* name = glfwGetGamepadName(GLFW_JOYSTICK_1);
        printf("[gamepad] %s connected — press Back (View) button or H to open help panel\n",
               name ? name : "controller");
    }

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

    // Optional startup preset from --preset NAME. If it doesn't resolve we
    // just warn and continue with defaults — not fatal.
    if (!g_cfg.preset.empty()) {
        std::string path = preset_resolve(g_cfg.preset);
        if (path.empty()) {
            fprintf(stderr, "[preset] '%s' not found — using defaults\n",
                    g_cfg.preset.c_str());
        } else if (preset_load(path)) {
            for (size_t i = 0; i < g_presetFiles.size(); i++)
                if (g_presetFiles[i] == path) { g_currentPreset = (int)i; break; }
            printf("[preset] loaded: %s\n", preset_current_name().c_str());
        }
    }

    if (g_cfg.playerFps > 0)
        printf("[fps] vsync %s, cap %d fps\n", g_cfg.vsync ? "on" : "off", g_cfg.playerFps);
    else
        printf("[fps] vsync %s, uncapped\n", g_cfg.vsync ? "on" : "off");

    double prevFrameStart = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        const double frameStart = glfwGetTime();
        const float dt = (float)(frameStart - prevFrameStart);
        prevFrameStart = frameStart;

        glfwPollEvents();

        // Toggle the bottom-right gamepad hint based on whether a pad is
        // plugged in. Keeps its state fresh so plugging/unplugging works.
        S.ov.setShowGamepadHint(glfwJoystickPresent(GLFW_JOYSTICK_1) != 0);

        // Gamepad + MIDI polling. Keyboard already came in via the key
        // callback inside glfwPollEvents. Gamepad context depends on the
        // help panel's state: menu view → nav bindings; section view or
        // help closed → active section drives the controller.
        BindContext gpCtx;
        if (S.ov.helpVisible() && !S.ov.inSectionView()) {
            gpCtx = CTX_MENU;
        } else {
            int idx = S.ov.activeSection();
            if (idx < 0) idx = 2;                 // default Warp
            gpCtx = (BindContext)(CTX_SEC_STATUS + idx);
        }
        g_input.pollGamepad(GLFW_JOYSTICK_1, dt, gpCtx);
        g_input.pollMidi(dt);

        // BPM clock — advances beat phase, fires beat events.
        update_bpm(frameStart, dt);

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

        // One-shot screenshot request from PrtSc (ACT_SCREENSHOT).
        if (S.screenshotPending) {
            S.screenshotPending = false;
            save_screenshot(latest.fbo, S.simW, S.simH);
        }

        // Help provider is pulled per-frame from inside Overlay::draw, so
        // values shown in a section stay live. Nothing to push here.
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
