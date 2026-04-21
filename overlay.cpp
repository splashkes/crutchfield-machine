// overlay.cpp — HUD + top-left drill-down help panel.

#include "overlay.h"
#include "stb_easy_font.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

// ── shaders for flat-coloured 2D text/rectangles ──────────────────────────
static const char* VS = R"(#version 460 core
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

static const char* FS = R"(#version 460 core
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
    if (!hudVisible && !helpVisible_) return;

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
        // No backing rect — white text on the live feedback. Deliberate.
        // Keep the text at full alpha against whatever colour is behind.
        for (int i = 0; i < nLines; i++) {
            const auto& line = lines_[nLines - 1 - i];
            float y = (float)(winH_ - bottomPad - (i+1) * lineH + 4);
            drawTextLine((float)leftPad, y, line.text, fg, alpha, TEXT_SCALE);
        }
    }

    // ---- Help panel (top-left, no dim) ----
    if (helpVisible_) drawHelpPanel();

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
