#pragma once
#include <GL/glew.h>
#include <cstdint>
#include <cstdio>
#include <string>

// HEVC MP4 recorder — pipes RGB frames straight into ffmpeg using the
// VideoToolbox hardware encoder. Designed to run alongside the EXR
// archive recorder, not replace it: EXR is for grade-from-source
// archival, MP4 is the "shareable in five seconds" path.
//
// Pipeline:
//   render thread → capture(srcFBO):
//     glReadPixels into a 2-PBO ring (RGB8, async)
//     map the older PBO → fwrite RGB bytes into ffmpeg's stdin pipe
//   ffmpeg → hevc_videotoolbox (HW) → output.mp4
//
// Synchronous fwrite is fine here: 1080p RGB8 ≈ 6 MB/frame at 60 fps
// ≈ 360 MB/s, which is well under the pipe's bandwidth ceiling on
// Apple Silicon. The PBO ring just hides the readback fence.

struct GLFWwindow;

class Mp4Recorder {
public:
    struct Config {
        // Container/codec choice. "hevc" → hevc_videotoolbox in .mp4.
        // "h264" → h264_videotoolbox in .mp4. "prores" → prores_videotoolbox
        // in .mov, profile 3 (HQ) for true grade-grade output.
        std::string codec = "hevc";
        // VideoToolbox quality knob in [0..100]. 65 is visually
        // transparent for most feedback content. ProRes ignores this
        // (profile picks the rate).
        int  quality = 65;
        // Where MP4s land. Empty = ~/Movies/CrutchfieldMachine.
        std::string outDir;
        // Override ffmpeg binary location. Empty = /opt/homebrew/bin/ffmpeg
        // → /usr/local/bin/ffmpeg → "ffmpeg" on PATH.
        std::string ffmpegPath;
    };

    bool start(int srcW, int srcH, int fps, const Config& cfg);
    void stop();   // closes the pipe, waits for ffmpeg to finalize

    // Read from the bound display FBO (or whichever GL_READ_FRAMEBUFFER
    // is the user-visible image) and ship the result down the pipe.
    void capture(GLuint srcFBO);

    bool        active()        const { return active_; }
    uint64_t    framesWritten() const { return framesWritten_; }
    const std::string& lastFile() const { return lastFile_; }

    // Cycle through codec presets at runtime (HEVC ↔ H264 ↔ ProRes).
    // Takes effect on the next start(); no-op while a recording is
    // already running.
    void cycleCodec();
    const std::string& codec() const { return cfg_.codec; }

private:
    static constexpr int N_PBO = 3;

    bool        active_ = false;
    FILE*       pipe_ = nullptr;
    GLuint      pbo_[N_PBO] = {0};
    int         w_ = 0, h_ = 0;
    int         pboCursor_ = 0;
    uint64_t    frameNum_ = 0;
    uint64_t    framesWritten_ = 0;

    Config      cfg_;
    std::string lastFile_;

    void shutdownPipe();
};
