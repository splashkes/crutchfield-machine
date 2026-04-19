// exr_write.h — minimal ZIP-compressed EXR writer for RGBA half-float images.
//
// Spec reference: https://openexr.com/en/latest/OpenEXRFileLayout.html
//
// Why no OpenEXR dependency:
//   The full OpenEXR library is ~500K LOC of C++. Scanline-based ZIP EXR is
//   header-attributes + per-chunk offsets + (reorder → delta predictor → zlib)
//   per 16-line block. Writing it ourselves keeps the build trivial. We only
//   depend on zlib (small, universally available).
//
// What we DON'T support:
//   - Tiled images, multipart files, deep data, non-HALF channels
//   - Compression types other than ZIP (scanline, 16 lines/block)
//
// What we DO support:
//   - 16-bit half-float RGBA, line-order INCREASING_Y
//   - ZIP_COMPRESSION (type 3) — reads correctly in Resolve, ffmpeg, Nuke, Blender.
//
// Half-float conversion: stb_image style; sufficient for normal-range values.

#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <zlib.h>

namespace exr {

// Convert IEEE 754 binary32 → binary16 (half-float).
// Round-to-nearest-even, with overflow → inf, underflow → 0/denormal.
inline uint16_t f32_to_f16(float f) {
    uint32_t x; std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t  exp  = ((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x007fffff;
    if (exp >= 31) {                  // overflow / inf / nan
        if ((x & 0x7fffffff) > 0x7f800000)  // nan
            return (uint16_t)(sign | 0x7e00);
        return (uint16_t)(sign | 0x7c00); // inf
    }
    if (exp <= 0) {                    // subnormal or zero
        if (exp < -10) return (uint16_t)sign;
        mant |= 0x00800000;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t halfMant = mant >> shift;
        // Round half to even
        if ((mant >> (shift - 1)) & 1) halfMant++;
        return (uint16_t)(sign | halfMant);
    }
    // Normal
    uint32_t halfMant = mant >> 13;
    if (mant & 0x00001000) halfMant++; // round
    if (halfMant & 0x00000400) {       // mantissa overflow into exponent
        halfMant = 0;
        exp++;
        if (exp >= 31) return (uint16_t)(sign | 0x7c00);
    }
    return (uint16_t)(sign | (exp << 10) | halfMant);
}

// EXR attribute writers. Each attribute is name\0type\0size(int32)\0data.
inline void wr_attr_str(FILE* f, const char* name, const char* type, const char* val) {
    int32_t sz = (int32_t)std::strlen(val) + 1;
    fwrite(name, 1, std::strlen(name) + 1, f);
    fwrite(type, 1, std::strlen(type) + 1, f);
    fwrite(&sz, 4, 1, f);
    fwrite(val, 1, sz, f);
}
inline void wr_attr_box2i(FILE* f, const char* name, int32_t x0, int32_t y0,
                          int32_t x1, int32_t y1) {
    int32_t sz = 16;
    fwrite(name, 1, std::strlen(name) + 1, f);
    fwrite("box2i", 1, 6, f);
    fwrite(&sz, 4, 1, f);
    fwrite(&x0, 4, 1, f); fwrite(&y0, 4, 1, f);
    fwrite(&x1, 4, 1, f); fwrite(&y1, 4, 1, f);
}
inline void wr_attr_v2f(FILE* f, const char* name, float a, float b) {
    int32_t sz = 8;
    fwrite(name, 1, std::strlen(name) + 1, f);
    fwrite("v2f", 1, 4, f);
    fwrite(&sz, 4, 1, f);
    fwrite(&a, 4, 1, f); fwrite(&b, 4, 1, f);
}
inline void wr_attr_float(FILE* f, const char* name, float v) {
    int32_t sz = 4;
    fwrite(name, 1, std::strlen(name) + 1, f);
    fwrite("float", 1, 6, f);
    fwrite(&sz, 4, 1, f);
    fwrite(&v, 4, 1, f);
}
inline void wr_attr_compression(FILE* f, uint8_t type) {
    int32_t sz = 1;
    fwrite("compression", 1, 12, f);
    fwrite("compression", 1, 12, f);
    fwrite(&sz, 4, 1, f);
    fwrite(&type, 1, 1, f);
}
inline void wr_attr_lineorder(FILE* f) {
    int32_t sz = 1;
    fwrite("lineOrder", 1, 10, f);
    fwrite("lineOrder", 1, 10, f);
    fwrite(&sz, 4, 1, f);
    uint8_t v = 0;   // INCREASING_Y
    fwrite(&v, 1, 1, f);
}
// channels: list of (name, pixelType=HALF=1, pLinear=0, xSamp=1, ySamp=1)
// terminated by a single null byte. Must be in alphabetical order: A, B, G, R.
inline void wr_attr_channels_RGBA(FILE* f) {
    auto chan = [&](const char* n) {
        fwrite(n, 1, std::strlen(n) + 1, f);
        int32_t pt = 1;       // HALF
        uint8_t pLin = 0;
        uint8_t pad[3] = {0, 0, 0};
        int32_t xy[2] = {1, 1};
        fwrite(&pt, 4, 1, f);
        fwrite(&pLin, 1, 1, f);
        fwrite(pad, 1, 3, f);
        fwrite(xy, 4, 2, f);
    };
    auto sizeOf = [&](const char* n) -> int32_t {
        return (int32_t)std::strlen(n) + 1 + 4 + 1 + 3 + 8;
    };
    int32_t sz = sizeOf("A") + sizeOf("B") + sizeOf("G") + sizeOf("R") + 1;
    fwrite("channels", 1, 9, f);
    fwrite("chlist", 1, 7, f);
    fwrite(&sz, 4, 1, f);
    chan("A"); chan("B"); chan("G"); chan("R");
    uint8_t term = 0;
    fwrite(&term, 1, 1, f);
}

// EXR ZIP preprocessor: byte-reorder (even/odd split) then delta-encode the
// reordered stream. Source is `src[0..n)`; output goes to `dst[0..n)`.
// Must be applied before zlib compress; inverse must be applied after
// zlib decompress by readers. Matches OpenEXR's Imf::Zip::compress exactly.
inline void zip_preprocess(const uint8_t* src, uint8_t* dst, size_t n) {
    size_t half = (n + 1) / 2;
    uint8_t* t1 = dst;
    uint8_t* t2 = dst + half;
    size_t i = 0;
    for (; i + 1 < n; i += 2) { *t1++ = src[i]; *t2++ = src[i + 1]; }
    if (n & 1) *t1 = src[n - 1];

    int p = dst[0];
    for (size_t j = 1; j < n; j++) {
        int d = (int)dst[j] - p + (128 + 256);
        p = dst[j];
        dst[j] = (uint8_t)d;
    }
}

enum class Compression : uint8_t { NONE = 0, ZIP = 3 };

// Write an RGBA half-float EXR. Pixels are tightly packed: `pixels[y*w + x]`
// is RGBA in that order, half-float (16-bit). GL convention is bottom-up;
// EXR's INCREASING_Y is top-to-bottom, so we flip as we build each chunk.
//
// Compression:
//   ZIP  — 16 scanlines/chunk, reorder+delta+zlib (Z_BEST_SPEED). ~2-3× smaller.
//   NONE — 1 scanline/chunk, raw bytes. Much faster to write, larger files.
inline bool write_rgba_half(const char* path, int w, int h, const uint16_t* rgba,
                            Compression comp = Compression::ZIP) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    uint32_t magic = 20000630;
    uint32_t version = 2;   // single-part scanline image
    std::fwrite(&magic, 4, 1, f);
    std::fwrite(&version, 4, 1, f);

    wr_attr_channels_RGBA(f);
    wr_attr_compression(f, (uint8_t)comp);
    wr_attr_box2i(f, "dataWindow",    0, 0, w - 1, h - 1);
    wr_attr_box2i(f, "displayWindow", 0, 0, w - 1, h - 1);
    wr_attr_lineorder(f);
    wr_attr_float(f, "pixelAspectRatio", 1.0f);
    wr_attr_v2f(f, "screenWindowCenter", 0.0f, 0.0f);
    wr_attr_float(f, "screenWindowWidth", 1.0f);

    uint8_t hdr_term = 0;
    std::fwrite(&hdr_term, 1, 1, f);

    // Spec: ZIP uses 16-line chunks; NO_COMPRESSION uses 1-line chunks.
    const int LINES_PER_CHUNK = (comp == Compression::ZIP) ? 16 : 1;
    const int nChunks = (h + LINES_PER_CHUNK - 1) / LINES_PER_CHUNK;
    const size_t scanlineBytes = (size_t)w * 4 * 2;  // RGBA × 2 bytes
    const size_t maxChunkBytes = scanlineBytes * LINES_PER_CHUNK;

    // Reserve the offset table; we come back after writing each chunk.
    long offsetTablePos = std::ftell(f);
    {
        uint64_t zero = 0;
        for (int c = 0; c < nChunks; c++) std::fwrite(&zero, 8, 1, f);
    }

    std::vector<uint8_t> uncompressed(maxChunkBytes);
    std::vector<uint8_t> preprocessed(comp == Compression::ZIP ? maxChunkBytes : 0);
    std::vector<uint8_t> compressed(comp == Compression::ZIP
                                    ? compressBound((uLong)maxChunkBytes) : 0);
    std::vector<uint64_t> offsets(nChunks);

    for (int c = 0; c < nChunks; c++) {
        const int yStart = c * LINES_PER_CHUNK;
        const int linesInChunk = (h - yStart < LINES_PER_CHUNK)
                                 ? (h - yStart) : LINES_PER_CHUNK;
        const size_t chunkBytes = scanlineBytes * linesInChunk;

        // Build the raw chunk. For each output scanline in this chunk,
        // plane-separate the source pixels into A,B,G,R order.
        // GL readback is bottom-up; EXR wants top-down.
        uint8_t* base = uncompressed.data();
        for (int ly = 0; ly < linesInChunk; ly++) {
            const int y = yStart + ly;
            const int srcRow = h - 1 - y;
            const uint16_t* src = rgba + (size_t)srcRow * w * 4;

            uint16_t* outA = (uint16_t*)(base + 0 * (size_t)w * 2);
            uint16_t* outB = (uint16_t*)(base + 1 * (size_t)w * 2);
            uint16_t* outG = (uint16_t*)(base + 2 * (size_t)w * 2);
            uint16_t* outR = (uint16_t*)(base + 3 * (size_t)w * 2);
            for (int x = 0; x < w; x++) {
                outR[x] = src[x*4 + 0];
                outG[x] = src[x*4 + 1];
                outB[x] = src[x*4 + 2];
                outA[x] = src[x*4 + 3];
            }
            base += scanlineBytes;
        }

        const uint8_t* outBuf;
        size_t outBytes;
        if (comp == Compression::ZIP) {
            // Reorder + delta, then zlib-compress.
            zip_preprocess(uncompressed.data(), preprocessed.data(), chunkBytes);
            uLong compSize = (uLong)compressed.size();
            int z = compress2(compressed.data(), &compSize,
                              preprocessed.data(), (uLong)chunkBytes,
                              Z_BEST_SPEED);
            if (z != Z_OK) { std::fclose(f); return false; }
            // EXR rule: if compressed isn't smaller, store uncompressed raw chunk.
            if ((size_t)compSize < chunkBytes) {
                outBuf = compressed.data();
                outBytes = (size_t)compSize;
            } else {
                outBuf = uncompressed.data();
                outBytes = chunkBytes;
            }
        } else {
            outBuf = uncompressed.data();
            outBytes = chunkBytes;
        }

        offsets[c] = (uint64_t)std::ftell(f);
        int32_t yi = yStart;
        int32_t dataSize = (int32_t)outBytes;
        std::fwrite(&yi, 4, 1, f);
        std::fwrite(&dataSize, 4, 1, f);
        std::fwrite(outBuf, 1, outBytes, f);
    }

    // Rewrite the offset table now that all chunk positions are known.
    std::fseek(f, offsetTablePos, SEEK_SET);
    for (int c = 0; c < nChunks; c++) std::fwrite(&offsets[c], 8, 1, f);

    std::fclose(f);
    return true;
}

} // namespace exr
