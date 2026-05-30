// text_render.cpp — stb_truetype-backed atlas font renderer.
//
// Bakes three font sizes (~14, ~22, ~36 px) into one 1024x1024 R8 atlas
// at init. Each draw call streams a small vertex buffer and renders
// textured quads with alpha blending. ASCII range 32..126 is enough
// for the UI text we use; non-ASCII falls back to space.

#include "text_render.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdint>

namespace TextRender {
namespace {

constexpr int   ATLAS_W = 1024;
constexpr int   ATLAS_H = 1024;
constexpr int   FIRST_CHAR = 32;
constexpr int   N_CHARS    = 95;     // 32..126 inclusive

// Pixel heights for the three sizes. Tuned for readability at 1x and
// retina; the user can scale framebuffer-wise via shader/projection
// without rasterizing more atlases.
constexpr float HEIGHTS[SZ_COUNT] = { 16.f, 24.f, 38.f };

struct State {
    bool   ready = false;
    int    winW = 1280, winH = 720;
    GLuint texAtlas = 0;
    GLuint prog = 0, vao = 0, vbo = 0;
    GLint  locRes = -1, locAlpha = -1, locTex = -1;
    stbtt_bakedchar charData[SZ_COUNT][N_CHARS];
};

State g;

const char* VS = R"(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uRes;
out vec2 vUV;
out vec4 vCol;
void main() {
    vec2 ndc = vec2(aPos.x / uRes.x * 2.0 - 1.0,
                    1.0 - aPos.y / uRes.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vCol = aCol;
}
)";

const char* FS = R"(#version 410 core
in  vec2 vUV;
in  vec4 vCol;
out vec4 oCol;
uniform sampler2D uTex;
uniform float uAlpha;
void main() {
    float a = texture(uTex, vUV).r;
    oCol = vec4(vCol.rgb, vCol.a * a * uAlpha);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "[text] shader compile: %s\n", log);
        return 0;
    }
    return s;
}

bool buildProgram() {
    GLuint vs = compile(GL_VERTEX_SHADER,   VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return false;
    g.prog = glCreateProgram();
    glAttachShader(g.prog, vs);
    glAttachShader(g.prog, fs);
    glLinkProgram(g.prog);
    GLint ok; glGetProgramiv(g.prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "[text] link failed\n"); return false; }
    glDeleteShader(vs);
    glDeleteShader(fs);
    g.locRes   = glGetUniformLocation(g.prog, "uRes");
    g.locAlpha = glGetUniformLocation(g.prog, "uAlpha");
    g.locTex   = glGetUniformLocation(g.prog, "uTex");
    return true;
}

bool buildAtlas(const std::vector<uint8_t>& ttf) {
    std::vector<uint8_t> bitmap(ATLAS_W * ATLAS_H, 0);
    // Bake all three sizes into the same atlas. stbtt_BakeFontBitmap
    // packs each rasterization starting from (0,0) — we offset by
    // band so they don't overlap.
    int yCursor = 0;
    for (int s = 0; s < SZ_COUNT; s++) {
        int bandH = ATLAS_H / SZ_COUNT;
        std::vector<uint8_t> band(ATLAS_W * bandH, 0);
        int rv = stbtt_BakeFontBitmap(ttf.data(), 0, HEIGHTS[s],
                                      band.data(), ATLAS_W, bandH,
                                      FIRST_CHAR, N_CHARS, g.charData[s]);
        if (rv <= 0 && rv > -N_CHARS) {
            fprintf(stderr, "[text] BakeFontBitmap partial at size %d (filled %d glyphs)\n",
                    s, -rv);
        }
        // Copy this band into the atlas at yCursor.
        for (int y = 0; y < bandH; y++) {
            std::memcpy(bitmap.data() + (yCursor + y) * ATLAS_W,
                        band.data()   + y * ATLAS_W, ATLAS_W);
        }
        // Adjust charData y-offsets to land in the atlas band.
        for (int c = 0; c < N_CHARS; c++) {
            g.charData[s][c].y0 += yCursor;
            g.charData[s][c].y1 += yCursor;
        }
        yCursor += bandH;
    }

    glGenTextures(1, &g.texAtlas);
    glBindTexture(GL_TEXTURE_2D, g.texAtlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    GLint swiz[4] = { GL_RED, GL_RED, GL_RED, GL_RED };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swiz);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

bool loadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    out.resize((size_t)sz);
    size_t rd = std::fread(out.data(), 1, (size_t)sz, f);
    std::fclose(f);
    return rd == (size_t)sz;
}

struct V {
    float x, y;
    float u, v;
    unsigned char c[4];
};

}  // namespace

bool init(const std::string& ttfPath) {
    if (g.ready) return true;
    std::vector<uint8_t> ttf;
    if (!loadFile(ttfPath, ttf)) {
        std::fprintf(stderr, "[text] cannot load font '%s'\n", ttfPath.c_str());
        return false;
    }
    if (!buildProgram()) return false;
    if (!buildAtlas(ttf)) return false;

    glGenBuffers(1, &g.vbo);
    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), (void*)16);

    g.ready = true;
    std::fprintf(stdout, "[text] font ready: %s (sizes 16/24/38)\n", ttfPath.c_str());
    return true;
}

void resize(int w, int h) { g.winW = w; g.winH = h; }
void shutdown() {
    if (g.vbo) { glDeleteBuffers(1, &g.vbo); g.vbo = 0; }
    if (g.vao) { glDeleteVertexArrays(1, &g.vao); g.vao = 0; }
    if (g.prog) { glDeleteProgram(g.prog); g.prog = 0; }
    if (g.texAtlas) { glDeleteTextures(1, &g.texAtlas); g.texAtlas = 0; }
    g.ready = false;
}
bool ready() { return g.ready; }

float lineHeight(Size sz) {
    return HEIGHTS[(int)sz] * 1.25f;
}

float width(const char* text, Size sz) {
    if (!text || !g.ready) return 0.f;
    float x = 0.f, y = 0.f;
    stbtt_aligned_quad q;
    for (const char* p = text; *p; p++) {
        int c = (unsigned char)*p;
        if (c < FIRST_CHAR || c >= FIRST_CHAR + N_CHARS) c = ' ';
        stbtt_GetBakedQuad(g.charData[(int)sz], ATLAS_W, ATLAS_H,
                           c - FIRST_CHAR, &x, &y, &q, 1);
    }
    return x;
}

void draw(float x, float y, const char* text, Size sz,
          const unsigned char rgba[4], float alpha)
{
    if (!text || !g.ready) return;
    std::vector<V> verts;
    verts.reserve(std::strlen(text) * 6);

    // y-baseline shift: stb's baked quads are anchored at baseline.
    // Top-left input maps to baseline ~= y + HEIGHTS[sz] * 0.8.
    float xc = x;
    float yc = y + HEIGHTS[(int)sz] * 0.85f;
    stbtt_aligned_quad q;
    for (const char* p = text; *p; p++) {
        int c = (unsigned char)*p;
        if (c < FIRST_CHAR || c >= FIRST_CHAR + N_CHARS) c = ' ';
        stbtt_GetBakedQuad(g.charData[(int)sz], ATLAS_W, ATLAS_H,
                           c - FIRST_CHAR, &xc, &yc, &q, 1);
        // Two triangles per glyph
        V v00 { q.x0, q.y0, q.s0, q.t0, {rgba[0], rgba[1], rgba[2], rgba[3]} };
        V v10 { q.x1, q.y0, q.s1, q.t0, {rgba[0], rgba[1], rgba[2], rgba[3]} };
        V v11 { q.x1, q.y1, q.s1, q.t1, {rgba[0], rgba[1], rgba[2], rgba[3]} };
        V v01 { q.x0, q.y1, q.s0, q.t1, {rgba[0], rgba[1], rgba[2], rgba[3]} };
        verts.push_back(v00); verts.push_back(v10); verts.push_back(v11);
        verts.push_back(v00); verts.push_back(v11); verts.push_back(v01);
    }
    if (verts.empty()) return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.texAtlas);
    glUseProgram(g.prog);
    glUniform1i(g.locTex, 0);
    glUniform2f(g.locRes, (float)g.winW, (float)g.winH);
    glUniform1f(g.locAlpha, alpha);
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(V)),
                 verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
}

void drawRight(float xRight, float y, const char* text, Size sz,
               const unsigned char rgba[4], float alpha)
{
    float w = width(text, sz);
    draw(xRight - w, y, text, sz, rgba, alpha);
}

}  // namespace TextRender
