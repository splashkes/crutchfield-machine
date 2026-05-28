// overlay.cpp — HUD + top-left drill-down help panel.

#include "overlay.h"
#include "stb_easy_font.h"
#include "text_render.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

// ── shaders for flat-coloured 2D text/rectangles ──────────────────────────
static const char* VS = R"(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main() {
    vec2 ndc = vec2(aPos.x / uRes.x * 2.0 - 1.0,
                    1.0 - aPos.y / uRes.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vCol = aCol;
}
)";

static const char* FS = R"(#version 410 core
in  vec4 vCol;
out vec4 oCol;
uniform float uAlpha;
void main() { oCol = vec4(vCol.rgb, vCol.a * uAlpha); }
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "[overlay] shader compile: %s\n", log);
        return 0;
    }
    return s;
}

bool Overlay::init() {
    GLuint vs = compile(GL_VERTEX_SHADER,   VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs); glLinkProgram(prog_);
    GLint ok; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "[overlay] link failed\n"); return false; }
    glDeleteShader(vs); glDeleteShader(fs);

    locRes_   = glGetUniformLocation(prog_, "uRes");
    locAlpha_ = glGetUniformLocation(prog_, "uAlpha");

    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // stb_easy_font format: 12 bytes position + 4 bytes RGBA = 16 bytes / vertex
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, (void*)12);
    return true;
}

void Overlay::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
}

void Overlay::resize(int w, int h) { winW_ = w; winH_ = h; }

// ── HUD fade timing ───────────────────────────────────────────────────────
static constexpr double FADE_START = 3.5;
static constexpr double FADE_DUR   = 1.5;
static constexpr double FADE_END   = FADE_START + FADE_DUR;
static constexpr float  TEXT_SCALE = 2.4f;

void Overlay::logParam(const std::string& key,
                       const std::string& label,
                       const std::string& deltaStr,
                       const std::string& valueStr)
{
    const double now = glfwGetTime();
    if (now - lastActivity_ > FADE_END) lines_.clear();
    lastActivity_ = now;

    char buf[256];
    snprintf(buf, sizeof buf, "%-10s %s   now %s",
             label.c_str(), deltaStr.c_str(), valueStr.c_str());
    std::string txt = buf;

    for (auto& l : lines_) {
        if (l.key == key) { l.text = txt; l.lastTouch = now; return; }
    }
    lines_.push_back({ key, txt, now });
}

void Overlay::logEvent(const std::string& text) {
    const double now = glfwGetTime();
    if (now - lastActivity_ > FADE_END) lines_.clear();
    lastActivity_ = now;
    lines_.push_back({ "", text, now });
}

// ── drawing helpers ───────────────────────────────────────────────────────
void Overlay::drawTextLine(float x, float y, const std::string& text,
                           unsigned char rgba[4], float alpha, float scale)
{
    const int CHARS_PER_BATCH = 256;
    const int VBUF_BYTES = CHARS_PER_BATCH * 270 + 1024;
    static std::vector<char> vbuf(VBUF_BYTES);
    static std::vector<float> tris;
    tris.reserve(CHARS_PER_BATCH * 6 * 4);

    glBindVertexArray(vao_);
    glUseProgram(prog_);
    glUniform2f(locRes_, (float)winW_, (float)winH_);
    glUniform1f(locAlpha_, alpha);

    float penX = x, penY = y;
    const float lineAdvance = 12.0f * scale;

    size_t i = 0;
    while (i < text.size()) {
        size_t end = i;
        size_t take = 0;
        while (end < text.size() && text[end] != '\n' && take < CHARS_PER_BATCH) {
            end++; take++;
        }

        if (end > i) {
            std::string seg = text.substr(i, end - i);
            int nq = stb_easy_font_print(penX, penY, (char*)seg.c_str(),
                                         rgba, vbuf.data(), (int)vbuf.size());
            if (nq > 0) {
                if (scale != 1.0f) {
                    float* fv = (float*)vbuf.data();
                    for (int k = 0; k < nq * 4; k++) {
                        fv[k*4 + 0] = penX + (fv[k*4 + 0] - penX) * scale;
                        fv[k*4 + 1] = penY + (fv[k*4 + 1] - penY) * scale;
                    }
                }
                tris.clear();
                tris.reserve(nq * 6 * 4);
                const float* qv = (const float*)vbuf.data();
                for (int q = 0; q < nq; q++) {
                    const float* q0 = qv + (q*4 + 0) * 4;
                    const float* q1 = qv + (q*4 + 1) * 4;
                    const float* q2 = qv + (q*4 + 2) * 4;
                    const float* q3 = qv + (q*4 + 3) * 4;
                    for (int v = 0; v < 4; v++) tris.push_back(q0[v]);
                    for (int v = 0; v < 4; v++) tris.push_back(q1[v]);
                    for (int v = 0; v < 4; v++) tris.push_back(q2[v]);
                    for (int v = 0; v < 4; v++) tris.push_back(q0[v]);
                    for (int v = 0; v < 4; v++) tris.push_back(q2[v]);
                    for (int v = 0; v < 4; v++) tris.push_back(q3[v]);
                }
                glBindBuffer(GL_ARRAY_BUFFER, vbo_);
                glBufferData(GL_ARRAY_BUFFER, tris.size() * sizeof(float),
                             tris.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(tris.size() / 4));
            }
            penX += stb_easy_font_width((char*)seg.c_str()) * scale;
        }

        if (end < text.size() && text[end] == '\n') {
            penX = x;
            penY += lineAdvance;
            end++;
        }
        i = end;
    }
}

void Overlay::drawTextBacked(float x, float y, const std::string& text,
                             unsigned char fg[4], float scale)
{
    // Per-line dark strip sized to the text's stb_easy_font_width. Skips
    // empty lines so gaps between rows let the feedback show through.
    unsigned char bg[4] = { 0, 0, 0, 255 };
    const float lineAdvance = 12.0f * scale;
    float py = y;
    size_t i = 0;
    while (true) {
        size_t nl = text.find('\n', i);
        std::string line = (nl == std::string::npos)
                             ? text.substr(i) : text.substr(i, nl - i);
        if (!line.empty()) {
            int tw = stb_easy_font_width((char*)line.c_str());
            float sw = tw * scale;
            drawFilledRect(x - 3.0f, py - 2.0f,
                           sw + 6.0f, 10.0f * scale + 3.0f,
                           bg, 0.60f);
        }
        if (nl == std::string::npos) break;
        i = nl + 1;
        py += lineAdvance;
    }
    drawTextLine(x, y, text, fg, 1.0f, scale);
}

void Overlay::drawFilledRect(float x, float y, float w, float h,
                             unsigned char rgba[4], float alpha)
{
    struct V { float x, y, z; unsigned char c[4]; };
    static_assert(sizeof(V) == 16, "V must be 16 bytes");

    V verts[6];
    const unsigned char r=rgba[0], g=rgba[1], b=rgba[2], a=rgba[3];
    V c0 = { x,     y,     0.f, {r,g,b,a} };
    V c1 = { x+w,   y,     0.f, {r,g,b,a} };
    V c2 = { x+w,   y+h,   0.f, {r,g,b,a} };
    V c3 = { x,     y+h,   0.f, {r,g,b,a} };
    verts[0]=c0; verts[1]=c1; verts[2]=c2;
    verts[3]=c0; verts[4]=c2; verts[5]=c3;

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glUseProgram(prog_);
    glUniform2f(locRes_, (float)winW_, (float)winH_);
    glUniform1f(locAlpha_, alpha);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// ── help panel state ──────────────────────────────────────────────────────
void Overlay::setHelpSections(std::vector<std::string> names) {
    sections_ = std::move(names);
    if (menuSel_ >= (int)sections_.size()) menuSel_ = 0;
}

void Overlay::toggleHelp() {
    helpVisible_ = !helpVisible_;
    if (!helpVisible_) {
        // Reset to menu so next open starts fresh.
        view_ = VIEW_MENU;
        sectionScroll_ = 0;
    }
}

void Overlay::helpUp() {
    if (view_ == VIEW_MENU) {
        if (!sections_.empty())
            menuSel_ = (menuSel_ - 1 + (int)sections_.size()) % (int)sections_.size();
    } else {
        if (sectionScroll_ > 0) sectionScroll_--;
    }
}

void Overlay::helpDown() {
    if (view_ == VIEW_MENU) {
        if (!sections_.empty())
            menuSel_ = (menuSel_ + 1) % (int)sections_.size();
    } else {
        sectionScroll_++;
        // Clamped in drawHelpSection against actual line count.
    }
}

void Overlay::helpEnter() {
    if (view_ == VIEW_MENU && !sections_.empty()) {
        view_ = VIEW_SECTION;
        sectionScroll_ = 0;
        activeSection_ = menuSel_;   // controller now drives this section
    }
}

void Overlay::helpBack() {
    if (view_ == VIEW_SECTION) { view_ = VIEW_MENU; sectionScroll_ = 0; }
    else                        { helpVisible_ = false; }
}

std::vector<std::string> Overlay::splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('\n', i);
        if (j == std::string::npos) { out.push_back(s.substr(i)); break; }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// ── the main draw ─────────────────────────────────────────────────────────
void Overlay::draw() {
    if (winW_ <= 0 || winH_ <= 0) return;

    // HUD alpha from fade curve.
    const double now = glfwGetTime();
    const double dt  = now - lastActivity_;
    float alpha = 1.0f;
    if (dt >= FADE_END)        alpha = 0.0f;
    else if (dt >= FADE_START) alpha = (float)(1.0 - (dt - FADE_START) / FADE_DUR);

    const bool hudVisible = (alpha > 0.001f) && !lines_.empty();
    if (!hudVisible && !helpVisible_ && !mathVisible_) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // ---- HUD bottom-left ----
    if (hudVisible) {
        const int lineH    = (int)(12 * TEXT_SCALE) + 4;
        const int leftPad  = 16;
        const int bottomPad= 16;
        unsigned char fg[4] = { 235, 235, 240, 255 };

        int nLines = (int)lines_.size();
        int visibleLines = std::min(nLines, 4);
        // No backing rect — white text on the live feedback. Deliberate.
        // Keep the text at full alpha against whatever colour is behind.
        for (int i = 0; i < visibleLines; i++) {
            const auto& line = lines_[nLines - 1 - i];
            float y = (float)(winH_ - bottomPad - (i+1) * lineH + 4);
            drawTextLine((float)leftPad, y, line.text, fg, alpha, TEXT_SCALE);
        }
    }

    // ---- Help panel (top-left, no dim) ----
    if (helpVisible_) drawHelpPanel();
    if (mathVisible_) drawMathPanel();

    // ---- Bottom-right gamepad hint (only when help is closed) ----
    if (!helpVisible_ && showGamepadHint_ && activeSection_ >= 0
        && activeSection_ < (int)sections_.size())
    {
        const float s = 1.4f;
        const float pad = 10.0f;
        char buf[128];
        snprintf(buf, sizeof buf, "%s  \x10  Back: help",
                 sections_[activeSection_].c_str());
        int txtW = stb_easy_font_width(buf);
        float w = txtW * s + pad * 2.0f;
        float h = 12.0f * s + pad;
        float x = (float)winW_ - w - 10.0f;
        float y = (float)winH_ - h - 10.0f;
        unsigned char bg[4]  = { 10, 12, 16, 170 };
        unsigned char fg[4]  = { 180, 190, 210, 255 };
        drawFilledRect(x, y, w, h, bg, 0.85f);
        drawTextLine(x + pad, y + pad * 0.5f, buf, fg, 1.0f, s);
    }

    glDisable(GL_BLEND);
}

// Panel geometry: fixed top-left. Scales modestly with window width so
// the panel isn't postage-stamp sized at 4K, but never grows past a
// readable ~540 px column.
void Overlay::drawHelpPanel() {
    const float pad       = 14.0f;
    const float panelX    = 12.0f;
    const float panelY    = 12.0f;
    const float panelW    = (winW_ >= 1920) ? 540.0f
                          : (winW_ >= 1280) ? 460.0f : 380.0f;
    const float panelH    = (winH_ >= 1080) ? 620.0f
                          : (winH_ >= 720)  ? 520.0f : 420.0f;

    // Panel background removed — each text line now gets its own tight
    // dark backing via drawTextBacked, so feedback shows through between
    // rows instead of behind an opaque block.

    if (view_ == VIEW_MENU) drawHelpMenu(panelX + pad, panelY + pad,
                                         panelW - pad*2, panelH - pad*2);
    else                    drawHelpSection(panelX + pad, panelY + pad,
                                            panelW - pad*2, panelH - pad*2);
}

void Overlay::drawHelpMenu(float x, float y, float w, float /*h*/) {
    (void)w;
    unsigned char title[4] = { 220, 230, 255, 255 };
    unsigned char hint[4]  = { 140, 150, 170, 255 };
    unsigned char sel[4]   = { 60, 120, 200, 230 };
    unsigned char selFG[4] = { 255, 255, 255, 255 };
    unsigned char fg[4]    = { 200, 205, 215, 255 };

    const float titleS = 1.6f;
    const float hintS  = 1.2f;
    const float rowS   = 1.6f;
    const float rowH   = 12.0f * rowS + 4.0f;

    drawTextBacked(x, y, "help   [H close]", title, titleS);
    drawTextBacked(x, y + 12.0f * titleS + 4.0f,
                   "Up/Down select   Enter drill in", hint, hintS);

    float rowY = y + 12.0f * titleS + 4.0f + 12.0f * hintS + 10.0f;
    for (int i = 0; i < (int)sections_.size(); i++) {
        if (i == menuSel_) {
            drawFilledRect(x - 4.0f, rowY - 2.0f,
                           12.0f * rowS * (int)sections_[i].size() + 24.0f,
                           rowH, sel, 0.85f);
            drawTextLine(x, rowY, sections_[i], selFG, 1.0f, rowS);
        } else {
            drawTextBacked(x, rowY, sections_[i], fg, rowS);
        }
        rowY += rowH;
    }
}

void Overlay::drawHelpSection(float x, float y, float w, float h) {
    unsigned char title[4] = { 220, 230, 255, 255 };
    unsigned char hint[4]  = { 140, 150, 170, 255 };
    unsigned char fg[4]    = { 200, 205, 215, 255 };
    unsigned char dim[4]   = { 130, 140, 150, 255 };

    const float titleS = 1.6f;
    const float hintS  = 1.2f;
    const float bodyS  = 1.3f;
    const float lineH  = 12.0f * bodyS + 2.0f;

    const std::string secName = (menuSel_ >= 0 && menuSel_ < (int)sections_.size())
                                ? sections_[menuSel_] : std::string();
    char tbuf[128];
    snprintf(tbuf, sizeof tbuf, "\x10 %s", secName.c_str());
    drawTextBacked(x, y, tbuf, title, titleS);
    drawTextBacked(x, y + 12.0f * titleS + 4.0f,
                   "Up/Down scroll   Esc back   H close",
                   hint, hintS);

    float bodyY = y + 12.0f * titleS + 4.0f + 12.0f * hintS + 10.0f;
    const float bodyH = h - (bodyY - y);
    int visibleLines = (int)(bodyH / lineH);
    if (visibleLines < 1) visibleLines = 1;

    std::string body;
    if (provider_) body = provider_(menuSel_);
    else           body = "(no provider)";
    cachedBody_ = body;

    std::vector<std::string> lines = splitLines(body);

    // Clamp scroll to the last page.
    int maxScroll = (int)lines.size() - visibleLines;
    if (maxScroll < 0) maxScroll = 0;
    if (sectionScroll_ > maxScroll) sectionScroll_ = maxScroll;
    if (sectionScroll_ < 0) sectionScroll_ = 0;

    const float colX = x;
    float lineY = bodyY;
    int end = (int)lines.size();
    int start = sectionScroll_;
    int shown = 0;
    for (int i = start; i < end && shown < visibleLines; i++, shown++) {
        // Rows starting with "-- " are section headings inside the body.
        bool isHead = (lines[i].size() >= 3 && lines[i].compare(0, 3, "-- ") == 0);
        drawTextBacked(colX, lineY, lines[i], isHead ? title : fg, bodyS);
        lineY += lineH;
    }

    // Legend: one block at the bottom showing current gamepad map.
    // Drawn above the scroll indicator. Styled with the hint colour.
    if (legend_) {
        std::string leg = legend_(menuSel_);
        if (!leg.empty()) {
            // Count legend lines to position above the bottom scroll hint.
            int nLegendLines = 1;
            for (char c : leg) if (c == '\n') nLegendLines++;
            const float legS = 1.2f;
            const float legH = 12.0f * legS + 2.0f;
            float legY = y + h - nLegendLines * legH - 12.0f * hintS - 6.0f;
            // Soft separator.
            unsigned char sep[4] = { 70, 75, 90, 255 };
            drawFilledRect(x, legY - 6.0f, w, 1.0f, sep, 0.6f);
            drawTextBacked(x, legY, leg, hint, legS);
        }
    }

    // Scroll indicator.
    if (maxScroll > 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "  -- %d/%d --", start, maxScroll);
        drawTextLine(x, y + h - 12.0f * hintS, buf, dim, 1.0f, hintS);
    }
    (void)w;
}

// Forward to use ActionId values from input.h without dragging the
// header into overlay.h (and keep overlay.h dep-free of the action
// catalogue). We re-state the integer values that match the enum.
#include "input.h"

// Forward declarations for the accessor functions defined further down
// (in the same anonymous namespace shared with fmt4 / drawMathPanel
// helpers). Keeping the row table near the public methods means we
// need the forward decls here.
namespace {
float acc_decay(const Overlay::MathSample& s);
float acc_blurX(const Overlay::MathSample& s);
float acc_blurY(const Overlay::MathSample& s);
float acc_chroma(const Overlay::MathSample& s);
float acc_gamma(const Overlay::MathSample& s);
float acc_sat(const Overlay::MathSample& s);
float acc_contrast(const Overlay::MathSample& s);
float acc_noise(const Overlay::MathSample& s);
float acc_couple(const Overlay::MathSample& s);
float acc_external(const Overlay::MathSample& s);
float acc_outfade(const Overlay::MathSample& s);
float acc_zoom(const Overlay::MathSample& s);
float acc_theta(const Overlay::MathSample& s);
}  // namespace

// Row table — shared between drawMathPanel (display) and the public
// mathSelectedActionDec/Inc accessors (so cursor nav and rendering
// agree on what's editable). Keep this in sync with the visual order
// in drawMathPanel.
namespace {
struct MathRow {
    const char* label;
    float (*accessor)(const Overlay::MathSample&);
    int   actionDec;   // ActionId to fire on Left arrow (decrement)
    int   actionInc;   // ActionId to fire on Right arrow (increment)
};
const MathRow MATH_ROWS[] = {
    { "decay",     acc_decay,     ACT_DECAY_DN,    ACT_DECAY_UP    },
    { "blur X",    acc_blurX,     ACT_BLURX_DN,    ACT_BLURX_UP    },
    { "blur Y",    acc_blurY,     ACT_BLURY_DN,    ACT_BLURY_UP    },
    { "sat",       acc_sat,       ACT_SAT_DN,      ACT_SAT_UP      },
    { "noise",     acc_noise,     ACT_NOISE_DN,    ACT_NOISE_UP    },
    { "couple",    acc_couple,    ACT_COUPLE_DN,   ACT_COUPLE_UP   },
    { "external",  acc_external,  ACT_EXTERNAL_DN, ACT_EXTERNAL_UP },
    { "zoom",      acc_zoom,      ACT_ZOOM_DN,     ACT_ZOOM_UP     },
    { "theta",     acc_theta,     ACT_THETA_DN,    ACT_THETA_UP    },
    { "chroma",    acc_chroma,    ACT_CHROMA_DN,   ACT_CHROMA_UP   },
    { "gamma",     acc_gamma,     ACT_GAMMA_DN,    ACT_GAMMA_UP    },
    { "contrast",  acc_contrast,  ACT_CONTRAST_DN, ACT_CONTRAST_UP },
};
constexpr int N_MATH_ROWS = (int)(sizeof(MATH_ROWS) / sizeof(MATH_ROWS[0]));
} // namespace

int Overlay::mathNumRows() const { return N_MATH_ROWS; }

int Overlay::mathSelectedActionDec() const {
    if (mathSelectedRow_ < 0 || mathSelectedRow_ >= N_MATH_ROWS) return 0;
    return MATH_ROWS[mathSelectedRow_].actionDec;
}
int Overlay::mathSelectedActionInc() const {
    if (mathSelectedRow_ < 0 || mathSelectedRow_ >= N_MATH_ROWS) return 0;
    return MATH_ROWS[mathSelectedRow_].actionInc;
}
void Overlay::mathSelectNext() {
    if (!mathVisible_) return;
    if (mathSelectedRow_ < 0) mathSelectedRow_ = 0;
    else mathSelectedRow_ = (mathSelectedRow_ + 1) % N_MATH_ROWS;
}
void Overlay::mathSelectPrev() {
    if (!mathVisible_) return;
    if (mathSelectedRow_ < 0) mathSelectedRow_ = 0;
    else mathSelectedRow_ = (mathSelectedRow_ - 1 + N_MATH_ROWS) % N_MATH_ROWS;
}

// ─────────────────────────────────────────────────────────────────────────
// Math dashboard — an analytical view of the running feedback system.
// Renders a translucent panel on the right side of the screen showing:
//   1. SYSTEM CHARACTERIZATION — dynamical-regime prediction derived
//      from the current parameter set (decay, blur, couple, noise).
//      Spectral radius estimate, memory half-life, diffusion strength,
//      stability classification.
//   2. PARAMETER READOUT — every continuous parameter with its current
//      value, mathematical symbol, and brief interpretation.
//   3. SPARKLINES — recent history (~4 seconds) of each parameter as a
//      compact line graph.
//
// The panel is read-only; parameters are still edited via keyboard,
// MIDI, OSC, or gamepad. This view explains what they MEAN.
// ─────────────────────────────────────────────────────────────────────────

void Overlay::mathPushFrame(const MathSample& s) {
    if (mathRing_.size() < (size_t)MATH_RING_CAP) {
        mathRing_.push_back(s);
        mathRingHead_ = (int)mathRing_.size() - 1;
    } else {
        mathRingHead_ = (mathRingHead_ + 1) % MATH_RING_CAP;
        mathRing_[mathRingHead_] = s;
    }
    if (mathRingCount_ < MATH_RING_CAP) mathRingCount_++;
}

void Overlay::drawSparkline(float x, float y, float w, float h,
                            float (*accessor)(const MathSample&),
                            float vmin, float vmax,
                            unsigned char rgba[4]) {
    if (mathRingCount_ < 2 || w <= 1.f || h <= 1.f) return;

    // Auto-scale if vmin == vmax
    bool autoscale = (vmin == vmax);
    if (autoscale) {
        vmin =  1e30f; vmax = -1e30f;
        for (int i = 0; i < mathRingCount_; i++) {
            float v = accessor(mathRing_[i]);
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        if (vmax - vmin < 1e-6f) { vmin -= 0.5f; vmax += 0.5f; }
    }
    float vrange = vmax - vmin;
    if (vrange < 1e-9f) vrange = 1.f;

    // Background fill
    unsigned char bg[4] = {0,0,0,180};
    drawFilledRect(x, y, w, h, bg, 0.6f);

    // Line strip: read N samples evenly from the ring, oldest → newest.
    // The ring is "linear" while mathRingCount_ < CAP, then circular.
    int N = mathRingCount_;
    int start = (mathRingCount_ == MATH_RING_CAP)
                ? (mathRingHead_ + 1) % MATH_RING_CAP : 0;

    struct V { float x, y, z; unsigned char c[4]; };
    std::vector<V> verts;
    verts.reserve((size_t)N * 2);
    for (int i = 1; i < N; i++) {
        int i0 = (start + i - 1) % MATH_RING_CAP;
        int i1 = (start + i) % MATH_RING_CAP;
        float v0 = accessor(mathRing_[i0]);
        float v1 = accessor(mathRing_[i1]);
        float x0 = x + ((float)(i - 1) / (float)(N - 1)) * w;
        float x1 = x + ((float)(i)     / (float)(N - 1)) * w;
        float y0 = y + h - ((v0 - vmin) / vrange) * h;
        float y1 = y + h - ((v1 - vmin) / vrange) * h;
        verts.push_back({ x0, y0, 0.f, {rgba[0],rgba[1],rgba[2],rgba[3]} });
        verts.push_back({ x1, y1, 0.f, {rgba[0],rgba[1],rgba[2],rgba[3]} });
    }
    if (verts.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(V)),
                 verts.data(), GL_STREAM_DRAW);
    glUseProgram(prog_);
    glUniform2f(locRes_, (float)winW_, (float)winH_);
    glUniform1f(locAlpha_, 1.0f);
    glLineWidth(1.5f);
    glDrawArrays(GL_LINES, 0, (GLsizei)verts.size());
}

namespace {
// Pretty-format a float in 1.4 chars budget.
std::string fmt4(float v) {
    char b[16];
    if (std::fabs(v) >= 10.f)        snprintf(b, sizeof b, "%6.2f", v);
    else if (std::fabs(v) >= 1.f)    snprintf(b, sizeof b, "%6.3f", v);
    else                              snprintf(b, sizeof b, "%6.4f", v);
    return b;
}
// Sample accessors for sparklines.
float acc_decay(const Overlay::MathSample& s) { return s.decay;   }
float acc_blurX(const Overlay::MathSample& s) { return s.blurX;   }
float acc_blurY(const Overlay::MathSample& s) { return s.blurY;   }
float acc_chroma(const Overlay::MathSample& s){ return s.chroma;  }
float acc_gamma(const Overlay::MathSample& s) { return s.gamma;   }
float acc_sat(const Overlay::MathSample& s)   { return s.satGain; }
float acc_contrast(const Overlay::MathSample& s){return s.contrast;}
float acc_noise(const Overlay::MathSample& s) { return s.noise;   }
float acc_couple(const Overlay::MathSample& s){ return s.couple;  }
float acc_external(const Overlay::MathSample& s){return s.external;}
float acc_outfade(const Overlay::MathSample& s){return s.outFade; }
float acc_zoom(const Overlay::MathSample& s)  { return s.zoom;    }
float acc_theta(const Overlay::MathSample& s) { return s.theta;   }
}

void Overlay::drawMathPanel() {
    if (mathRingCount_ == 0) return;
    const MathSample& cur = mathRing_[mathRingHead_];

    const bool useTTF = TextRender::ready();

    // Text drawer that picks TTF if available, otherwise falls back to
    // stb_easy_font with a comparable scale.
    auto tx = [&](float x, float y, const char* text, TextRender::Size sz,
                  unsigned char rgba[4], float alpha = 1.f) {
        if (useTTF) {
            TextRender::draw(x, y, text, sz, rgba, alpha);
        } else {
            float scale = (sz == TextRender::SZ_LARGE)  ? 2.4f
                        : (sz == TextRender::SZ_MEDIUM) ? 1.7f : 1.3f;
            drawTextLine(x, y, std::string(text), rgba, alpha, scale);
        }
    };
    auto txR = [&](float xRight, float y, const char* text, TextRender::Size sz,
                   unsigned char rgba[4], float alpha = 1.f) {
        if (useTTF) {
            TextRender::drawRight(xRight, y, text, sz, rgba, alpha);
        } else {
            // Crude right-align: skip computing width, just left-align at xRight-160
            float scale = (sz == TextRender::SZ_LARGE)  ? 2.4f
                        : (sz == TextRender::SZ_MEDIUM) ? 1.7f : 1.3f;
            drawTextLine(xRight - 160.f, y, std::string(text), rgba, alpha, scale);
        }
    };
    auto lh = [&](TextRender::Size sz) {
        if (useTTF) return TextRender::lineHeight(sz);
        return (sz == TextRender::SZ_LARGE) ? 36.f
             : (sz == TextRender::SZ_MEDIUM) ? 26.f : 18.f;
    };

    // ── Geometry ──────────────────────────────────────────────────────
    const float panelW = std::min(880.f, (float)winW_ * 0.50f);
    const float panelX = (float)winW_ - panelW - 24.f;
    const float panelY = 24.f;
    const float panelH = std::min((float)winH_ - 48.f, 1080.f);

    unsigned char bg[4]      = { 8, 12, 20, 240 };
    unsigned char accent[4]  = { 80, 180, 250, 255 };
    unsigned char titleC[4]  = { 240, 244, 252, 255 };
    unsigned char headingC[4]= { 100, 200, 255, 255 };
    unsigned char valueC[4]  = { 230, 236, 248, 255 };
    unsigned char dimC[4]    = { 140, 158, 188, 255 };
    unsigned char warnC[4]   = { 250, 180,  90, 255 };
    unsigned char dangerC[4] = { 248, 110, 110, 255 };
    unsigned char okC[4]     = { 130, 220, 150, 255 };

    // Background + accents
    drawFilledRect(panelX, panelY, panelW, panelH, bg, 0.94f);
    drawFilledRect(panelX, panelY, panelW, 4.f, accent, 1.f);
    drawFilledRect(panelX + panelW - 56.f, panelY + 14.f, 44.f, 4.f, accent, 0.6f);

    float x = panelX + 28.f;
    float y = panelY + 26.f;

    // Title
    tx(x, y, "MATHLAB", TextRender::SZ_LARGE, titleC);
    y += lh(TextRender::SZ_LARGE);
    tx(x, y, "feedback dynamics  ·  M to close",
       TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_SMALL) + 18.f;

    // ── Derived quantities ───────────────────────────────────────────
    float halflife_frames = (cur.decay > 0.f && cur.decay < 1.f)
        ? std::log(0.5f) / std::log(cur.decay) : 1e9f;
    float halflife_sec    = halflife_frames / 60.f;
    float D   = 0.5f * (cur.blurX * cur.blurX + cur.blurY * cur.blurY) * 0.5f;
    float rho = cur.decay * (1.f - 0.02f * (cur.blurX + cur.blurY));
    float noise_db = (cur.noise > 1e-9f) ? 20.f * std::log10(cur.noise) : -120.f;
    float Kc  = cur.couple;
    float hue_dps = cur.hueRate * 60.f * 360.f;

    const char* regime;
    unsigned char* regColor = okC;
    if (rho > 1.001f)        { regime = "DIVERGENT";  regColor = dangerC; }
    else if (rho > 0.998f)   { regime = "MARGINAL";   regColor = warnC;   }
    else if (Kc > 0.6f)      { regime = "CHAOTIC";    regColor = dangerC; }
    else if (Kc > 0.3f)      { regime = "TURBULENT";  regColor = warnC;   }
    else                     { regime = "STABLE";     regColor = okC;     }

    char buf[160];

    // REGIME headline
    tx(x, y, "REGIME", TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_SMALL);
    tx(x, y, regime, TextRender::SZ_LARGE, regColor);
    y += lh(TextRender::SZ_LARGE) + 18.f;

    // ── characterization rows ───────────────────────────────────────
    tx(x, y, "characterization", TextRender::SZ_MEDIUM, headingC);
    y += lh(TextRender::SZ_MEDIUM) + 6.f;

    const float valRightX = panelX + panelW - 28.f;
    auto row2 = [&](const char* label, const char* val) {
        tx (x, y, label, TextRender::SZ_SMALL, dimC);
        txR(valRightX, y, val, TextRender::SZ_SMALL, valueC);
        y += lh(TextRender::SZ_SMALL) + 4.f;
    };

    snprintf(buf, sizeof buf, "%.4f", rho);
    row2("rho  spectral radius", buf);

    if (halflife_frames > 9999.f) snprintf(buf, sizeof buf, "infinite");
    else snprintf(buf, sizeof buf, "%.0f frames  /  %.2f s", halflife_frames, halflife_sec);
    row2("memory half-life", buf);

    snprintf(buf, sizeof buf, "%.4f", D);
    row2("diffusion D", buf);

    snprintf(buf, sizeof buf, "%.4f", Kc);
    row2("coupling Kc", buf);

    snprintf(buf, sizeof buf, "%.4f   (%.1f dB)", cur.noise, noise_db);
    row2("noise floor", buf);

    snprintf(buf, sizeof buf, "%.1f deg / sec", hue_dps);
    row2("hue rotation", buf);

    y += 14.f;

    // ── Parameter editor ─────────────────────────────────────────────
    tx(x, y, "parameters", TextRender::SZ_MEDIUM, headingC);
    tx(x + 200.f, y + 4.f, "up/down select  ·  left/right adjust",
       TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_MEDIUM) + 10.f;

    const float labelW = 140.f;
    const float valueW = 110.f;
    const float sparkX = x + labelW + valueW + 14.f;
    const float sparkW = (panelX + panelW - 28.f) - sparkX;
    const float rowH   = lh(TextRender::SZ_MEDIUM) + 12.f;
    const float hlPadX = 10.f;

    unsigned char selBg[4]    = { 32, 72, 110, 230 };
    unsigned char selLabel[4] = { 255, 255, 255, 255 };
    unsigned char selValue[4] = { 255, 255, 255, 255 };
    unsigned char selSpark[4] = { 170, 230, 255, 255 };

    for (int i = 0; i < N_MATH_ROWS; i++) {
        if (y + rowH > panelY + panelH - lh(TextRender::SZ_SMALL) - 24.f) break;
        const MathRow& r = MATH_ROWS[i];
        float v = r.accessor(cur);

        bool isSel = (i == mathSelectedRow_);
        unsigned char* lblC = isSel ? selLabel : dimC;
        unsigned char* valC = isSel ? selValue : valueC;
        unsigned char* spkC = isSel ? selSpark : accent;

        if (isSel) {
            drawFilledRect(panelX + hlPadX, y - 4.f,
                           panelW - 2.f * hlPadX, rowH,
                           selBg, 0.7f);
        }

        float labelX = x + (isSel ? 14.f : 0.f);
        tx(labelX, y, r.label, TextRender::SZ_MEDIUM, lblC);
        snprintf(buf, sizeof buf, "%.4f", v);
        txR(sparkX - 14.f, y, buf, TextRender::SZ_MEDIUM, valC);

        drawSparkline(sparkX, y + 4.f, sparkW, rowH - 12.f,
                      r.accessor, 0.f, 0.f, spkC);
        y += rowH;
    }

    // Footer
    float footY = panelY + panelH - lh(TextRender::SZ_SMALL) - 12.f;
    tx(panelX + 28.f, footY,
       "shift+arrow = 20x coarse step  ·  M closes",
       TextRender::SZ_SMALL, dimC);
}
