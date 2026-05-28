// text_render.h — proper TTF text renderer for UI panels.
//
// stb_truetype-backed atlas rasterizer. Used by the Mathlab overlay
// (and any future polished UI) where stb_easy_font's wireframe glyphs
// look too rough.
//
// Three font sizes are baked at init: small (label/note), medium (body),
// large (heading/title). All glyphs land in a single 1024x1024 R8 alpha
// texture; draw calls stream textured quads. Self-contained — no
// external font config needed.

#pragma once

#include <GL/glew.h>
#include <cstdint>
#include <string>

namespace TextRender {

enum Size {
    SZ_SMALL  = 0,    // ~14 px
    SZ_MEDIUM = 1,    // ~22 px
    SZ_LARGE  = 2,    // ~36 px
    SZ_COUNT  = 3
};

// Initialize the renderer. ttfPath is the path to a TTF/OTF font.
// Returns true on success. winW/winH are the framebuffer dims (used to
// build the projection matrix; can be updated at runtime via resize()).
bool init(const std::string& ttfPath);

void resize(int winW, int winH);
void shutdown();

bool ready();

// Measure a string at the given size. Returns pixel width.
float width(const char* text, Size size);

// Draw a string at (x, y), top-left aligned. RGBA in 0..255.
void draw(float x, float y, const char* text, Size size,
          const unsigned char rgba[4], float alpha = 1.0f);

// Draw right-aligned: the END of the string lands at x.
void drawRight(float xRight, float y, const char* text, Size size,
               const unsigned char rgba[4], float alpha = 1.0f);

// Line height in pixels at the given size.
float lineHeight(Size size);

} // namespace TextRender
