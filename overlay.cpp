// overlay.cpp — HUD + top-left drill-down help panel.

#include "overlay.h"
#include "stb_easy_font.h"
#include "text_render.h"
#include "dynamics.h"

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
    int   actionSet;   // ActionId for direct-set (mouse drag). 0 = no direct set.
    float vmin, vmax;  // slider range. The setAxis actions all expect 0..1.
                       // For display range (knob position) we use 0..1 too;
                       // the engine clamps internally if needed.
};
const MathRow MATH_ROWS[] = {
    { "decay",     acc_decay,     ACT_DECAY_DN,    ACT_DECAY_UP,    ACT_DECAY_AXIS,        0.f, 1.f },
    { "blur X",    acc_blurX,     ACT_BLURX_DN,    ACT_BLURX_UP,    ACT_BLURX_SET_AXIS,    0.f, 1.f },
    { "blur Y",    acc_blurY,     ACT_BLURY_DN,    ACT_BLURY_UP,    ACT_BLURY_SET_AXIS,    0.f, 1.f },
    { "sat",       acc_sat,       ACT_SAT_DN,      ACT_SAT_UP,      ACT_SAT_SET_AXIS,      0.f, 1.f },
    { "noise",     acc_noise,     ACT_NOISE_DN,    ACT_NOISE_UP,    ACT_NOISE_AXIS,        0.f, 1.f },
    { "couple",    acc_couple,    ACT_COUPLE_DN,   ACT_COUPLE_UP,   ACT_COUPLE_SET_AXIS,   0.f, 1.f },
    { "external",  acc_external,  ACT_EXTERNAL_DN, ACT_EXTERNAL_UP, ACT_EXTERNAL_AXIS,     0.f, 1.f },
    { "zoom",      acc_zoom,      ACT_ZOOM_DN,     ACT_ZOOM_UP,     ACT_ZOOM_AXIS,         0.f, 1.f },
    { "theta",     acc_theta,     ACT_THETA_DN,    ACT_THETA_UP,    ACT_THETA_AXIS,        0.f, 1.f },
    { "chroma",    acc_chroma,    ACT_CHROMA_DN,   ACT_CHROMA_UP,   0,                     0.f, 1.f },
    { "gamma",     acc_gamma,     ACT_GAMMA_DN,    ACT_GAMMA_UP,    ACT_GAMMA_SET_AXIS,    0.f, 1.f },
    { "contrast",  acc_contrast,  ACT_CONTRAST_DN, ACT_CONTRAST_UP, ACT_CONTRAST_SET_AXIS, 0.f, 1.f },
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

// ── Mouse routing through the hit list ──────────────────────────────
static int math_hit_test(const std::vector<Overlay::MathHit>& hits,
                         double mx, double my) {
    for (size_t i = 0; i < hits.size(); i++) {
        const auto& h = hits[i];
        if (mx >= h.x && mx <= h.x + h.w && my >= h.y && my <= h.y + h.h)
            return (int)i;
    }
    return -1;
}

static void math_queue_slider_value(std::vector<Overlay::PendingDispatch>& q,
                                    const Overlay::MathHit& h,
                                    double mx, int actionId) {
    float t = (float)((mx - h.x) / h.w);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    q.push_back({ actionId, t });
}

bool Overlay::mathMouseDown(double mx, double my) {
    if (!mathVisible_) return false;
    int hit = math_hit_test(mathHits_, mx, my);
    if (hit < 0) return false;
    mathActiveHit_ = hit;
    mathDragArmed_ = true;
    const MathHit& h = mathHits_[hit];

    // Buttons fire on press immediately; sliders/pads queue values.
    switch (h.kind) {
    case MHIT_SLIDER_DIST:
        // fire ACT_REGIME_DISTANCE_AXIS — see Math/regime.distance.axis
        math_queue_slider_value(mathPending_, h, mx, /*ACT_REGIME_DISTANCE_AXIS*/
                                ACT_REGIME_DISTANCE_AXIS);
        break;
    case MHIT_SLIDER_HALFLIFE:
        math_queue_slider_value(mathPending_, h, mx, ACT_DYN_HALFLIFE_AXIS);
        break;
    case MHIT_PAD_COMPASS: {
        float tx = (float)((mx - h.x) / h.w);
        float ty = (float)((my - h.y) / h.h);
        if (tx < 0.f) tx = 0.f; if (tx > 1.f) tx = 1.f;
        if (ty < 0.f) ty = 0.f; if (ty > 1.f) ty = 1.f;
        // Pad: visual Y axis is inverted (top of screen is +Y in pad sense).
        ty = 1.0f - ty;
        mathPending_.push_back({ ACT_PAD_REGIME_X, tx });
        mathPending_.push_back({ ACT_PAD_REGIME_Y, ty });
        break;
    }
    case MHIT_BUTTON_REGIME:
        mathPending_.push_back({ ACT_REGIME_SET, (float)h.value });
        break;
    case MHIT_BUTTON_INVERT:
        mathPending_.push_back({ ACT_REGIME_INVERT, 1.0f });
        break;
    case MHIT_BUTTON_FAILSAFE:
        mathPending_.push_back({ ACT_THEATER_FAILSAFE, 1.0f });
        break;
    case MHIT_BUTTON_ECHO:
        mathPending_.push_back({ ACT_MATH_ECHO_TOGGLE, 1.0f });
        break;
    case MHIT_BUTTON_SNAP_SAVE:
        mathPending_.push_back({ ACT_SNAPSHOT_SAVE, (float)h.value });
        break;
    case MHIT_BUTTON_SNAP_RECALL_STABLE:
        // We don't have a regime-tagged recall action exposed, so fire
        // snapshot.recall on slot 1 as a sane default for one-click
        // recovery. Users can rebind via [osc] for richer behavior.
        mathPending_.push_back({ ACT_SNAPSHOT_RECALL, 1.0f });
        break;
    case MHIT_NONE:
    default: break;
    }
    return true;
}

bool Overlay::mathMouseDrag(double mx, double my) {
    if (!mathVisible_ || !mathDragArmed_ || mathActiveHit_ < 0) return false;
    if (mathActiveHit_ >= (int)mathHits_.size()) return false;
    const MathHit& h = mathHits_[mathActiveHit_];
    switch (h.kind) {
    case MHIT_SLIDER_DIST:
        math_queue_slider_value(mathPending_, h, mx, ACT_REGIME_DISTANCE_AXIS);
        return true;
    case MHIT_SLIDER_HALFLIFE:
        math_queue_slider_value(mathPending_, h, mx, ACT_DYN_HALFLIFE_AXIS);
        return true;
    case MHIT_PAD_COMPASS: {
        float tx = (float)((mx - h.x) / h.w);
        float ty = (float)((my - h.y) / h.h);
        if (tx < 0.f) tx = 0.f; if (tx > 1.f) tx = 1.f;
        if (ty < 0.f) ty = 0.f; if (ty > 1.f) ty = 1.f;
        ty = 1.0f - ty;
        mathPending_.push_back({ ACT_PAD_REGIME_X, tx });
        mathPending_.push_back({ ACT_PAD_REGIME_Y, ty });
        return true;
    }
    default:
        return true;  // press-only hits absorb the drag but do nothing
    }
}

void Overlay::mathMouseUp() {
    mathDragArmed_ = false;
    mathActiveHit_ = -1;
}

void Overlay::mathTickDrag(const std::function<void(int, float)>& dispatch) {
    if (mathPending_.empty()) return;
    auto pending = std::move(mathPending_);
    mathPending_.clear();
    for (const auto& p : pending) dispatch(p.actionId, p.value);
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

    // Reset hit list — drawMathPanel is the source of truth for what's
    // clickable this frame.
    mathHits_.clear();

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

    float x = panelX + 24.f;
    float y = panelY + 22.f;

    // Title
    tx(x, y, "DYNAMICS", TextRender::SZ_LARGE, titleC);
    tx(x + 220.f, y + 12.f, "the cockpit  ·  M to close",
       TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_LARGE) + 8.f;

    // ── Derived quantities ───────────────────────────────────────────
    // Pure math lives in dynamics.h. Both this display and the snapshot
    // tagger in main.cpp::classify_regime call into the same functions
    // so the regime label here always matches what FAILSAFE recalls.
    float halflife_frames = dyn::compute_halflife_frames(cur.decay);
    float halflife_sec    = halflife_frames / 60.f;
    float D   = dyn::compute_diffusion(cur.blurX, cur.blurY);
    float rho = dyn::compute_rho(cur.decay, cur.blurX, cur.blurY);
    float noise_db = (cur.noise > 1e-9f) ? 20.f * std::log10(cur.noise) : -120.f;
    float Kc  = cur.couple;
    float hue_dps = cur.hueRate * 60.f * 360.f;

    int regimeCode = dyn::classify_regime(rho, Kc);
    const char* regime = dyn::regime_name(regimeCode);
    unsigned char* regColor = okC;
    switch (regimeCode) {
        case dyn::DIVERGENT: regColor = dangerC; break;
        case dyn::CHAOTIC:   regColor = dangerC; break;
        case dyn::MARGINAL:  regColor = warnC;   break;
        case dyn::TURBULENT: regColor = warnC;   break;
        case dyn::STABLE:    regColor = okC;     break;
    }

    char buf[160];

    // ── REGIME header with a "you are here" position bar ────────────
    // The bar visualizes regime distance along [0..1] with threshold
    // markers at 0.5 (TURB) and 1.0 (CHAOS).
    {
        float barX = x;
        float barY = y;
        float barW = panelW - 48.f;
        float barH = 22.f;
        unsigned char barBg[4]   = { 24, 34, 50, 255 };
        drawFilledRect(barX, barY, barW, barH, barBg, 1.f);

        // 4 segments: STABLE (green), TURBULENT (orange), CHAOTIC (red), MARGINAL (yellow strip on right)
        unsigned char cStable[4] = { 130, 220, 150,  90 };
        unsigned char cTurb[4]   = { 245, 175,  90,  90 };
        unsigned char cChaos[4]  = { 248, 110, 110,  90 };
        drawFilledRect(barX,             barY, barW * 0.50f, barH, cStable, 0.35f);
        drawFilledRect(barX + barW*0.5f, barY, barW * 0.40f, barH, cTurb,   0.35f);
        drawFilledRect(barX + barW*0.9f, barY, barW * 0.10f, barH, cChaos,  0.35f);

        // "You are here" marker — position by regime-distance estimate.
        // Use a piecewise mapping that's the inverse of the
        // regime.distance.axis path so this reads correctly.
        float t = 0.f;
        if (Kc < 0.35f) t = (Kc - 0.05f) / (0.35f - 0.05f) * 0.5f;
        else            t = 0.5f + (Kc - 0.35f) / (0.70f - 0.35f) * 0.5f;
        if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
        unsigned char markerC[4] = { 255, 255, 255, 255 };
        drawFilledRect(barX + barW * t - 3.f, barY - 4.f, 6.f, barH + 8.f, markerC, 1.f);

        // Tiny regime label inside the bar (right-aligned)
        txR(barX + barW - 8.f, barY + 4.f, regime, TextRender::SZ_SMALL, regColor);
        y += barH + 12.f;
    }

    // Two-column row: "characterization metrics" + "live derived"
    {
        const float colW = (panelW - 56.f) * 0.5f;
        float cx = x;
        // Left column — math metrics
        tx(cx, y, "ρ", TextRender::SZ_SMALL, dimC);
        snprintf(buf, sizeof buf, "%.4f", rho);
        txR(cx + colW - 4.f, y, buf, TextRender::SZ_SMALL, valueC);
        // Right column — half-life
        cx = x + colW + 24.f;
        tx(cx, y, "half-life", TextRender::SZ_SMALL, dimC);
        if (halflife_frames > 9999.f) snprintf(buf, sizeof buf, "∞");
        else snprintf(buf, sizeof buf, "%.2f s", halflife_sec);
        txR(cx + colW - 4.f, y, buf, TextRender::SZ_SMALL, valueC);
        y += lh(TextRender::SZ_SMALL) + 2.f;

        cx = x;
        tx(cx, y, "K_c (coupling)", TextRender::SZ_SMALL, dimC);
        snprintf(buf, sizeof buf, "%.3f", Kc);
        txR(cx + colW - 4.f, y, buf, TextRender::SZ_SMALL, valueC);
        cx = x + colW + 24.f;
        tx(cx, y, "noise", TextRender::SZ_SMALL, dimC);
        snprintf(buf, sizeof buf, "%.1f dB", noise_db);
        txR(cx + colW - 4.f, y, buf, TextRender::SZ_SMALL, valueC);
        y += lh(TextRender::SZ_SMALL) + 14.f;
    }

    // ── INTERACTIVE: walk-to-chaos slider ─────────────────────────────
    tx(x, y, "walk to chaos", TextRender::SZ_MEDIUM, headingC);
    tx(x + 220.f, y + 6.f,
       "drag to set regime distance (0 = deep stable, 1 = deep chaotic)",
       TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_MEDIUM) + 4.f;
    {
        float sx = x, sy = y, sw = panelW - 48.f, sh = 28.f;
        unsigned char trackBg[4]   = { 22, 30, 44, 255 };
        unsigned char trackFill[4] = { 100, 200, 255, 255 };
        unsigned char trackTick[4] = { 245, 175,  90, 255 };
        unsigned char trackTick2[4]= { 248, 110, 110, 255 };
        drawFilledRect(sx, sy, sw, sh, trackBg, 1.f);
        // Threshold ticks
        drawFilledRect(sx + sw * 0.50f - 1.f, sy - 4.f, 2.f, sh + 8.f, trackTick, 1.f);
        drawFilledRect(sx + sw * 1.00f - 2.f, sy - 4.f, 2.f, sh + 8.f, trackTick2,1.f);
        // Estimated current position (uses the same inverse-mapping as the regime bar)
        float t = 0.f;
        if (Kc < 0.35f) t = (Kc - 0.05f) / (0.35f - 0.05f) * 0.5f;
        else            t = 0.5f + (Kc - 0.35f) / (0.70f - 0.35f) * 0.5f;
        if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
        drawFilledRect(sx, sy, sw * t, sh, trackFill, 0.7f);
        unsigned char knob[4] = { 245, 250, 255, 255 };
        drawFilledRect(sx + sw * t - 4.f, sy - 4.f, 8.f, sh + 8.f, knob, 1.f);
        // Hit
        mathHits_.push_back({ MHIT_SLIDER_DIST, sx, sy - 4.f, sw, sh + 8.f, 0 });
        y += sh + 14.f;
    }

    // ── INTERACTIVE: half-life slider ────────────────────────────────
    tx(x, y, "memory", TextRender::SZ_MEDIUM, headingC);
    {
        float bigHL = halflife_sec;
        if (bigHL > 9999.f) snprintf(buf, sizeof buf, "%s", "∞");
        else if (bigHL >= 1.f) snprintf(buf, sizeof buf, "%.2f s", bigHL);
        else snprintf(buf, sizeof buf, "%.0f ms", bigHL * 1000.f);
        txR(panelX + panelW - 28.f, y + 6.f, buf, TextRender::SZ_MEDIUM, valueC);
    }
    y += lh(TextRender::SZ_MEDIUM) + 4.f;
    {
        float sx = x, sy = y, sw = panelW - 48.f, sh = 22.f;
        unsigned char trackBg[4]   = { 22, 30, 44, 255 };
        unsigned char trackFill[4] = { 100, 200, 255, 255 };
        drawFilledRect(sx, sy, sw, sh, trackBg, 1.f);
        // Estimated position from current decay (inverse of log map)
        float tHL = 0.f;
        if (cur.decay > 0.f && cur.decay < 1.f) {
            float h_sec = (std::log(0.5f) / std::log(cur.decay)) / 60.f;
            // log map: 0.05..10  ->  0..1
            tHL = std::log(h_sec / 0.05f) / std::log(200.f);
            if (tHL < 0.f) tHL = 0.f; if (tHL > 1.f) tHL = 1.f;
        }
        drawFilledRect(sx, sy, sw * tHL, sh, trackFill, 0.7f);
        unsigned char knob[4] = { 245, 250, 255, 255 };
        drawFilledRect(sx + sw * tHL - 4.f, sy - 4.f, 8.f, sh + 8.f, knob, 1.f);
        mathHits_.push_back({ MHIT_SLIDER_HALFLIFE, sx, sy - 4.f, sw, sh + 8.f, 0 });
        y += sh + 14.f;
    }

    // ── INTERACTIVE: regime jump buttons + invert ─────────────────────
    tx(x, y, "jump", TextRender::SZ_MEDIUM, headingC);
    y += lh(TextRender::SZ_MEDIUM) + 4.f;
    {
        const char* names[4] = { "STABLE", "TURBULENT", "CHAOTIC", "MARGINAL" };
        unsigned char colors[4][4] = {
            { 130, 220, 150, 255 },
            { 245, 175,  90, 255 },
            { 248, 110, 110, 255 },
            { 250, 200, 110, 255 },
        };
        float bw = (panelW - 48.f - 18.f) / 5.f;
        float bh = 30.f;
        for (int i = 0; i < 4; i++) {
            float bx = x + i * (bw + 6.f);
            unsigned char bg[4] = { colors[i][0], colors[i][1], colors[i][2], 255 };
            drawFilledRect(bx, y, bw, bh, bg, 0.30f);
            unsigned char fg[4] = { colors[i][0], colors[i][1], colors[i][2], 255 };
            tx(bx + 8.f, y + 6.f, names[i], TextRender::SZ_SMALL, fg);
            mathHits_.push_back({ MHIT_BUTTON_REGIME, bx, y, bw, bh, i });
        }
        // Invert button on the right
        float bx = x + 4 * (bw + 6.f);
        unsigned char fg[4] = { 200, 220, 245, 255 };
        unsigned char bg[4] = { 40, 60, 90, 255 };
        drawFilledRect(bx, y, bw, bh, bg, 0.6f);
        tx(bx + 8.f, y + 6.f, "INVERT", TextRender::SZ_SMALL, fg);
        mathHits_.push_back({ MHIT_BUTTON_INVERT, bx, y, bw, bh, 0 });
        y += bh + 14.f;
    }

    // ── INTERACTIVE: regime compass pad ───────────────────────────────
    tx(x, y, "compass", TextRender::SZ_MEDIUM, headingC);
    tx(x + 140.f, y + 6.f,
       "drag inside the pad to walk through regimes",
       TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_MEDIUM) + 6.f;
    {
        float pw = 200.f, ph = 200.f;
        float px = x;
        float py = y;
        // Pad background with 4 quadrant tints (cardinal regimes)
        unsigned char qStable[4]  = { 130, 220, 150, 255 };
        unsigned char qTurb[4]    = { 245, 175,  90, 255 };
        unsigned char qChaos[4]   = { 248, 110, 110, 255 };
        unsigned char qMarg[4]    = { 250, 200, 110, 255 };
        // Quadrants: NE=Stable, NW=Turb, SW=Chaos, SE=Marginal (just a visual scheme)
        drawFilledRect(px + pw*0.5f, py,          pw*0.5f, ph*0.5f, qStable, 0.30f);  // NE
        drawFilledRect(px,           py,          pw*0.5f, ph*0.5f, qTurb,   0.30f);  // NW
        drawFilledRect(px,           py + ph*0.5f, pw*0.5f, ph*0.5f, qChaos,  0.30f);  // SW
        drawFilledRect(px + pw*0.5f, py + ph*0.5f, pw*0.5f, ph*0.5f, qMarg,   0.30f);  // SE
        // Crosshair
        unsigned char cross[4] = { 80, 100, 130, 255 };
        drawFilledRect(px + pw*0.5f - 0.5f, py, 1.f, ph, cross, 0.7f);
        drawFilledRect(px, py + ph*0.5f - 0.5f, pw, 1.f, cross, 0.7f);
        // Cardinal labels
        unsigned char labelC[4] = { 220, 230, 245, 255 };
        tx(px + pw - 70.f, py + 8.f, "STABLE", TextRender::SZ_SMALL, labelC);
        tx(px + 8.f,       py + 8.f, "TURB",   TextRender::SZ_SMALL, labelC);
        tx(px + 8.f,       py + ph - 22.f, "CHAOS", TextRender::SZ_SMALL, labelC);
        tx(px + pw - 70.f, py + ph - 22.f, "MARG",  TextRender::SZ_SMALL, labelC);
        // Hit region
        mathHits_.push_back({ MHIT_PAD_COMPASS, px, py, pw, ph, 0 });

        // Right of pad: toggles + snapshots panel
        float rx = px + pw + 24.f;
        float ry = py;
        float rh_btn = 32.f;
        float rw_btn = panelW - 48.f - (pw + 24.f);
        // failsafe button
        {
            extern bool g_failsafe_enabled;
            bool on = g_failsafe_enabled;
            unsigned char bgOn[4]  = { 130, 220, 150, 255 };
            unsigned char bgOff[4] = {  40,  56,  80, 255 };
            unsigned char fgOn[4]  = {  20,  40,  30, 255 };
            unsigned char fgOff[4] = { 200, 220, 245, 255 };
            drawFilledRect(rx, ry, rw_btn, rh_btn, on ? bgOn : bgOff, on ? 0.85f : 0.85f);
            tx(rx + 10.f, ry + 8.f,
               on ? "FAILSAFE  armed" : "FAILSAFE",
               TextRender::SZ_SMALL, on ? fgOn : fgOff);
            mathHits_.push_back({ MHIT_BUTTON_FAILSAFE, rx, ry, rw_btn, rh_btn, 0 });
            ry += rh_btn + 8.f;
        }
        // math.echo button
        {
            extern bool g_math_echo_enabled;
            bool on = g_math_echo_enabled;
            unsigned char bgOn[4]  = { 100, 200, 255, 255 };
            unsigned char bgOff[4] = {  40,  56,  80, 255 };
            unsigned char fgOn[4]  = {  10,  20,  40, 255 };
            unsigned char fgOff[4] = { 200, 220, 245, 255 };
            drawFilledRect(rx, ry, rw_btn, rh_btn, on ? bgOn : bgOff, 0.85f);
            tx(rx + 10.f, ry + 8.f,
               on ? "MATH ECHO  on" : "MATH ECHO",
               TextRender::SZ_SMALL, on ? fgOn : fgOff);
            mathHits_.push_back({ MHIT_BUTTON_ECHO, rx, ry, rw_btn, rh_btn, 0 });
            ry += rh_btn + 14.f;
        }
        // Snapshot quick buttons
        tx(rx, ry, "snapshots", TextRender::SZ_SMALL, dimC);
        ry += lh(TextRender::SZ_SMALL) + 4.f;
        {
            float sbw = (rw_btn - 18.f) / 4.f;
            for (int i = 1; i <= 4; i++) {
                float sbx = rx + (i - 1) * (sbw + 6.f);
                unsigned char bg[4] = { 45, 60, 85, 255 };
                drawFilledRect(sbx, ry, sbw, rh_btn, bg, 0.85f);
                char b[8]; snprintf(b, sizeof b, "%d", i);
                unsigned char fg[4] = { 220, 230, 245, 255 };
                tx(sbx + sbw * 0.5f - 6.f, ry + 8.f, b, TextRender::SZ_SMALL, fg);
                mathHits_.push_back({ MHIT_BUTTON_SNAP_SAVE, sbx, ry, sbw, rh_btn, i });
            }
            ry += rh_btn + 8.f;
        }
        // Recall STABLE
        {
            unsigned char bg[4] = { 30, 80, 50, 255 };
            unsigned char fg[4] = { 200, 240, 215, 255 };
            drawFilledRect(rx, ry, rw_btn, rh_btn, bg, 0.85f);
            tx(rx + 10.f, ry + 8.f, "RECALL  slot 1", TextRender::SZ_SMALL, fg);
            mathHits_.push_back({ MHIT_BUTTON_SNAP_RECALL_STABLE, rx, ry, rw_btn, rh_btn, 0 });
            ry += rh_btn;
        }
        y = py + ph + 14.f;
    }

    // ── footer hint ──────────────────────────────────────────────────
    tx(x, y,
       "Press H for the parameter editor.  Math echo publishes /cma/math/* at 30 Hz.",
       TextRender::SZ_SMALL, dimC);
    y += lh(TextRender::SZ_SMALL) + 4.f;

    // The interactive cockpit above is the whole panel — no more
    // billboard sparkline strip.
    (void)y;  // suppress unused-after-last-section warning
}
