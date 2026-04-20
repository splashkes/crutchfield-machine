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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>

#include "camera.h"

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
    L_ALL      = (1<<10) - 1
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
};

// ── framebuffer-object helper ─────────────────────────────────────────────
struct FBO { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };

static void resize_fbo(FBO& f, int w, int h) {
    if (!f.fbo) glGenFramebuffers(1, &f.fbo);
    if (!f.tex) glGenTextures(1, &f.tex);
    f.w = w; f.h = h;
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
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
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "can't open %s\n", path.c_str()); exit(1); }
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

static GLuint compile_shader(GLenum type, const std::string& src, const char* tag) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "[%s] compile error:\n%s\n", tag, log);
        // dump source with line numbers to help debugging
        std::istringstream is(src); std::string line; int n = 1;
        while (std::getline(is, line))
            fprintf(stderr, "%4d | %s\n", n++, line.c_str());
        exit(1);
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs, const char* tag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fprintf(stderr, "[%s] link error: %s\n", tag, log); exit(1);
    }
    return p;
}

static GLuint build_feedback_program() {
    std::string vs_src = read_file("shaders/main.vert");
    std::string fs_src = resolve_includes("shaders/main.frag");
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src, "main.vert");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, "main.frag");
    GLuint p = link_program(vs, fs, "feedback");
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

static GLuint build_blit_program() {
    std::string vs_src = read_file("shaders/main.vert");
    std::string fs_src = read_file("shaders/blit.frag");
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src, "main.vert");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, "blit.frag");
    GLuint p = link_program(vs, fs, "blit");
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ── global state ──────────────────────────────────────────────────────────
struct State {
    int winW = 1280, winH = 720;
    FBO A0, A1, B0, B1;       // two ping-pong pairs
    bool writeA = true;
    int  enable = L_WARP | L_OPTICS | L_COLOR | L_CONTRAST
                | L_DECAY | L_NOISE /* couple/external/inject off by default */;
    Params p;
    bool paused = false;
    int  selectedLayer = 0;   // for context-sensitive param adjustment
    bool needClear = true;
    uint32_t frame = 0;

    Camera  cam;
    GLuint  camTex = 0;
    std::vector<uint8_t> camBuf;
    bool    camReady = false;
};
static State S;

// ── help text ─────────────────────────────────────────────────────────────
static void print_help() {
    printf(
      "\n=== video feedback ===\n"
      " F1..F10  toggle layer (warp, optics, gamma, color, contrast,\n"
      "          decay, noise, couple, external, inject)\n"
      " L        reload shaders from disk (live edit!)\n"
      " C        clear both fields to black\n"
      " P        pause/resume\n"
      " SPACE    inject current pattern (hold)\n"
      " 1..5     pattern: H-bars, V-bars, dot, checker, gradient\n"
      " ?        this help\n"
      " Esc      quit\n\n"
      " --- parameter adjustments (hold Shift for coarse steps) ---\n"
      " Q/A   zoom            W/S   rotation       arrows  translate\n"
      " [/]   chroma         ;/'   blur-X          ,/.   blur-Y\n"
      " -/=   blur-angle     G/B   gamma\n"
      " E/D   hue rate       R/F   saturation\n"
      " T/Y   contrast       U/J   decay           N/M   noise\n"
      " K/,   couple amt     O/L   external amt\n\n");
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

// ── keyboard ──────────────────────────────────────────────────────────────
static GLuint progFeedback = 0, progBlit = 0;

static void reload_shaders() {
    GLuint np = build_feedback_program();
    if (progFeedback) glDeleteProgram(progFeedback);
    progFeedback = np;
    printf("[shaders] reloaded\n"); fflush(stdout);
}

static void key_cb(GLFWwindow* w, int key, int, int action, int mods) {
    const bool press  = (action == GLFW_PRESS);
    const bool rept   = (action == GLFW_PRESS || action == GLFW_REPEAT);
    const bool shift  = (mods & GLFW_MOD_SHIFT) != 0;
    const float s     = shift ? 0.010f : 0.002f;

    // layer toggles
    for (const auto& li : LAYERS)
        if (press && key == li.fkey) {
            S.enable ^= li.bit;
            printf("[%s] %s\n", li.name, (S.enable & li.bit) ? "ON" : "off");
            return;
        }

    if (press) switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); return;
        case GLFW_KEY_P: S.paused = !S.paused; return;
        case GLFW_KEY_C: S.needClear = true; return;
        case GLFW_KEY_L: reload_shaders(); return;
        case GLFW_KEY_SLASH: print_help(); return;   // '?' key (shifted /)
        case GLFW_KEY_SPACE: S.p.inject = 1.0f; return;
        case GLFW_KEY_1: S.p.pattern = 0; return;
        case GLFW_KEY_2: S.p.pattern = 1; return;
        case GLFW_KEY_3: S.p.pattern = 2; return;
        case GLFW_KEY_4: S.p.pattern = 3; return;
        case GLFW_KEY_5: S.p.pattern = 4; return;
    }
    if (action == GLFW_RELEASE && key == GLFW_KEY_SPACE) S.p.inject = 0.3f;

    if (rept) {
        auto& p = S.p;
        switch (key) {
            // warp
            case GLFW_KEY_Q: p.zoom  += s*5; break;
            case GLFW_KEY_A: p.zoom  -= s*5; break;
            case GLFW_KEY_W: p.theta += s;   break;
            case GLFW_KEY_S: p.theta -= s;   break;
            case GLFW_KEY_LEFT:  p.transX -= s; break;
            case GLFW_KEY_RIGHT: p.transX += s; break;
            case GLFW_KEY_UP:    p.transY -= s; break;
            case GLFW_KEY_DOWN:  p.transY += s; break;
            // optics
            case GLFW_KEY_LEFT_BRACKET:  p.chroma -= s * 0.5f; break;
            case GLFW_KEY_RIGHT_BRACKET: p.chroma += s * 0.5f; break;
            case GLFW_KEY_SEMICOLON:     p.blurX  -= s*5; break;
            case GLFW_KEY_APOSTROPHE:    p.blurX  += s*5; break;
            case GLFW_KEY_COMMA:         p.blurY  -= s*5; break;
            case GLFW_KEY_PERIOD:        p.blurY  += s*5; break;
            case GLFW_KEY_MINUS:         p.blurAngle -= s*5; break;
            case GLFW_KEY_EQUAL:         p.blurAngle += s*5; break;
            // gamma
            case GLFW_KEY_G: p.gamma += s*10; break;
            case GLFW_KEY_B: p.gamma  = fmaxf(0.1f, p.gamma - s*10); break;
            // color
            case GLFW_KEY_E: p.hueRate += s; break;
            case GLFW_KEY_D: p.hueRate -= s; break;
            case GLFW_KEY_R: p.satGain += s; break;
            case GLFW_KEY_F: p.satGain -= s; break;
            // contrast
            case GLFW_KEY_T: p.contrast += s*5; break;
            case GLFW_KEY_Y: p.contrast -= s*5; break;
            // decay
            case GLFW_KEY_U: p.decay = fminf(1.0f, p.decay + s); break;
            case GLFW_KEY_J: p.decay = fmaxf(0.9f, p.decay - s); break;
            // noise
            case GLFW_KEY_N: p.noise += s * 0.5f; break;
            case GLFW_KEY_M: p.noise = fmaxf(0.0f, p.noise - s * 0.5f); break;
            // couple / external
            case GLFW_KEY_K: p.couple   = fminf(1.0f, p.couple   + s*5); break;
            case GLFW_KEY_O: p.external = fminf(1.0f, p.external + s*5); break;
            case GLFW_KEY_I: p.couple   = fmaxf(0.0f, p.couple   - s*5); break;
            default: return;
        }
        print_status();
    }
}

static void size_cb(GLFWwindow*, int w, int h) {
    S.winW = w; S.winH = h;
    resize_fbo(S.A0, w, h); resize_fbo(S.A1, w, h);
    resize_fbo(S.B0, w, h); resize_fbo(S.B1, w, h);
    S.needClear = true;
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
    U1f("uGamma", p.gamma);
    U1f("uHueRate", p.hueRate); U1f("uSatGain", p.satGain);
    U1f("uContrast", p.contrast);
    U1f("uDecay", p.decay);
    U1f("uNoise", p.noise);
    U1f("uCouple", p.couple);
    U1f("uExternal", p.external);
    U1f("uInject", p.inject);
    U1i("uPattern", p.pattern);

    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ──────────────────────────────────────────────────────────────────────────
int main() {
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(S.winW, S.winH, "video feedback", nullptr, nullptr);
    if (!win) { fprintf(stderr, "window failed\n"); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "glew failed\n"); return 1; }

    glfwSetKeyCallback(win, key_cb);
    glfwSetFramebufferSizeCallback(win, size_cb);

    progFeedback = build_feedback_program();
    progBlit     = build_blit_program();

    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);

    int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
    size_cb(win, fbw, fbh);

    // Camera setup (optional; may fail silently).
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

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        if (S.needClear) {
            clear_fbo(S.A0); clear_fbo(S.A1);
            clear_fbo(S.B0); clear_fbo(S.B1);
            S.needClear = false;
        }

        // Pump camera (non-blocking).
        if (S.camReady && S.cam.grab(S.camBuf.data())) {
            glBindTexture(GL_TEXTURE_2D, S.camTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            S.cam.width(), S.cam.height(),
                            GL_RGB, GL_UNSIGNED_BYTE, S.camBuf.data());
        }

        if (!S.paused) {
            FBO& aSrc = S.writeA ? S.A1 : S.A0;
            FBO& aDst = S.writeA ? S.A0 : S.A1;
            FBO& bSrc = S.writeA ? S.B1 : S.B0;
            FBO& bDst = S.writeA ? S.B0 : S.B1;

            // Field A always updates. Field B only if coupling is on (otherwise
            // it sits in its initial state and no one cares).
            render_field(0, aSrc, aDst, bSrc);
            if (S.enable & L_COUPLE) render_field(1, bSrc, bDst, aSrc);

            S.writeA = !S.writeA;
            S.frame++;
        }

        // Blit latest field A to screen.
        FBO& latest = S.writeA ? S.A1 : S.A0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, S.winW, S.winH);
        glUseProgram(progBlit);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, latest.tex);
        glUniform1i(glGetUniformLocation(progBlit, "uSrc"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(win);

        // Inject is momentary.
        S.p.inject *= 0.85f;
        if (S.p.inject < 0.001f) S.p.inject = 0.0f;
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
