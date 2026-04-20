// overlay.cpp — HUD + help screen using stb_easy_font.

#include "overlay.h"
#include "stb_easy_font.h"

#include <GLFW/glfw3.h>    // glfwGetTime
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
    // pixel coords (top-left origin) → NDC
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
    // Leave the overlay's VAO bound here. The caller (main) rebinds its own
    // VAO at the top of every frame loop iteration, so it's safe. Earlier we
    // were calling glBindVertexArray(0), which unbound main's VAO that had
    // been set up before init() — and from then on, glDrawArrays calls in
    // the feedback pass silently failed on some drivers and rendered junk
    // on others, which is why the loop looked 'totally different'.
    return true;
}

void Overlay::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
}

void Overlay::resize(int w, int h) { winW_ = w; winH_ = h; }

// ── public posting API ────────────────────────────────────────────────────
// Fade timing: HUD stays at full opacity for FADE_START seconds after the
// last keypress, then fades linearly over FADE_DUR seconds. Fully invisible
// at FADE_START + FADE_DUR — which the user expects to be 5 seconds.
static constexpr double FADE_START = 3.5;
static constexpr double FADE_DUR   = 1.5;
static constexpr double FADE_END   = FADE_START + FADE_DUR;  // = 5.0

// Text magnification for the HUD and help overlay. stb_easy_font is tiny
// natively (~6x10 px/char); 2.4x gives ~14x24 which reads well at 1280+ wide.
static constexpr float TEXT_SCALE = 2.4f;

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

    // Replace existing line for this key if present (cumulative aggregation).
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
    // Render chunk-by-chunk so we never bail mid-text on long strings (the
    // help screen is ~5000 chars and would exceed any reasonable static buf).
    const int CHARS_PER_BATCH = 256;
    const int VBUF_BYTES = CHARS_PER_BATCH * 270 + 1024; // stb's worst-case per-char
    static std::vector<char> vbuf(VBUF_BYTES);
    static std::vector<float> tris;
    tris.reserve(CHARS_PER_BATCH * 6 * 4);

    glBindVertexArray(vao_);
    glUseProgram(prog_);
    glUniform2f(locRes_, (float)winW_, (float)winH_);
    glUniform1f(locAlpha_, alpha);

    // Walk the text, breaking at newlines and at CHARS_PER_BATCH chunks
    // (whichever comes first). Track current pen position so multi-batch
    // lines line up correctly.
    float penX = x, penY = y;
    const float lineAdvance = 12.0f * scale;

    size_t i = 0;
    while (i < text.size()) {
        // Take up to CHARS_PER_BATCH chars on the current line.
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
                // Per-call scaling about (penX, penY).
                if (scale != 1.0f) {
                    float* fv = (float*)vbuf.data();
                    for (int k = 0; k < nq * 4; k++) {
                        fv[k*4 + 0] = penX + (fv[k*4 + 0] - penX) * scale;
                        fv[k*4 + 1] = penY + (fv[k*4 + 1] - penY) * scale;
                    }
                }
                // Quads → triangles
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
            // Advance pen X by the rendered width.
            penX += stb_easy_font_width((char*)seg.c_str()) * scale;
        }

        if (end < text.size() && text[end] == '\n') {
            penX = x;
            penY += lineAdvance;
            end++; // consume the newline
        }
        i = end;
    }
}

void Overlay::drawFilledRect(float x, float y, float w, float h,
                             unsigned char rgba[4], float alpha)
{
    // V must be exactly 16 bytes to match the VAO's stride (3 floats for
    // pos+z, then 4 bytes RGBA). If you collapse the z the attrib pointer
    // reads garbage on every vertex past the first — which is why the help
    // box and HUD background were previously rendering as nonsense shapes.
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
    // Deliberately NOT calling glBindVertexArray(0) — leaving our own VAO
    // bound is harmless; unbinding would break the feedback render that
    // runs next frame if main hasn't rebound its own VAO yet.
}

// ── help text ─────────────────────────────────────────────────────────────
// ── help text comes from the host (main.cpp builds it dynamically with
//    the current values of every parameter, then calls ov.setHelpText) ──

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

    // Fast-path: nothing to draw. Leave blend state clean.
    if (!hudVisible && !helpVisible_) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // ---- HUD bottom-left ----
    if (hudVisible) {
        // stb glyph cell is ~10 px tall at 1x. At TEXT_SCALE the line box is
        // ~10*scale; we add a small gutter so lines don't touch.
        const int lineH    = (int)(12 * TEXT_SCALE) + 4;
        const int leftPad  = 16;
        const int bottomPad= 16;
        unsigned char bg[4] = { 0, 0, 0, 200 };
        unsigned char fg[4] = { 235, 235, 240, 255 };

        int nLines = (int)lines_.size();
        float boxH = (float)(nLines * lineH + 16);
        float boxY = (float)(winH_ - bottomPad - boxH + 6);
        float boxW = (float)(winW_ / 2 + 80);   // generous; won't exceed half-screen
        drawFilledRect(6, boxY, boxW, boxH, bg, alpha * 0.85f);

        // Newest line at the bottom of the stack, reading up.
        for (int i = 0; i < nLines; i++) {
            const auto& line = lines_[nLines - 1 - i];
            float y = (float)(winH_ - bottomPad - (i+1) * lineH + 4);
            drawTextLine((float)leftPad, y, line.text, fg, alpha, TEXT_SCALE);
        }
    }

    // ---- Help overlay ----
    if (helpVisible_) {
        const char* text = helpText_.empty() ? "(help text not set)" : helpText_.c_str();

        // Pick a scale that fits the help text into ~85% of the window in
        // both dimensions. Bigger window → bigger help. Floor at 2.4
        // (HUD-size) so the help is always at least as readable as the HUD,
        // ceiling at 6.0 so it doesn't get cartoonishly huge on 4K displays.
        int maxLineW = 0;
        int nLines   = 1;
        const char* p = text;
        const char* s = p;
        while (*p) {
            if (*p == '\n') {
                std::string seg(s, p);
                int w = stb_easy_font_width((char*)seg.c_str());
                if (w > maxLineW) maxLineW = w;
                nLines++;
                s = p + 1;
            }
            p++;
        }
        {
            std::string seg(s, p);
            int w = stb_easy_font_width((char*)seg.c_str());
            if (w > maxLineW) maxLineW = w;
        }

        const float pad = 32.0f;
        const float availW = winW_ * 0.92f - pad*2;
        const float availH = winH_ * 0.92f - pad*2;
        float scaleX = availW / (float)maxLineW;
        float scaleY = availH / (float)(nLines * 12);
        float scale  = scaleX < scaleY ? scaleX : scaleY;
        if (scale < 2.4f) scale = 2.4f;
        if (scale > 6.0f) scale = 6.0f;

        float textW = maxLineW * scale;
        float textH = (nLines * 12) * scale;
        float boxW  = textW + pad*2;
        float boxH  = textH + pad*2;
        if (boxW > winW_  - 20) boxW = (float)(winW_  - 20);
        if (boxH > winH_  - 20) boxH = (float)(winH_  - 20);
        float boxX = (winW_ - boxW) * 0.5f;
        float boxY = (winH_ - boxH) * 0.5f;

        unsigned char dim[4]   = { 0, 0, 0, 200 };
        unsigned char boxBG[4] = { 14, 14, 18, 245 };
        unsigned char fg[4]    = { 240, 240, 245, 255 };
        drawFilledRect(0, 0, (float)winW_, (float)winH_, dim, 0.80f);
        drawFilledRect(boxX, boxY, boxW, boxH, boxBG, 0.97f);
        drawTextLine(boxX + pad, boxY + pad, text, fg, 1.0f, scale);
    }

    glDisable(GL_BLEND);
}
