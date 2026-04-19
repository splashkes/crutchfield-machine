#pragma once
#include <GL/glew.h>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>

// Lossless EXR-sequence recorder. Captures RGBA half-float frames straight
// from the float sim FBO with NO format conversion in the read path. Each
// recording is a directory of frame_NNNNNN.exr files plus manifest.txt.
// Compress to MP4 later with ffmpeg if you want to share.
//
// Render thread cost per frame:
//   - 1× glReadPixels(GL_RGBA, GL_HALF_FLOAT) into PBO  (async)
//   - 1× glFenceSync                                     (async)
//   That's it. No format conversion, no map, no I/O, no encode.
//
// Writer thread (with its own GL context) handles:
//   - glClientWaitSync on fence
//   - glMapBuffer on PBO
//   - memcpy to staging
//   - exr::write_rgba_half() to disk

class GLFWwindow;

class Recorder {
public:
    // precision: 16 or 32. Selects PBO readback format (GL_HALF_FLOAT or GL_FLOAT)
    // so the render thread does not trigger a driver-side format conversion.
    bool start(int srcW, int srcH, int fps, int precision, GLFWwindow* mainWin);
    void stop();
    void capture(GLuint srcFBO);

    bool active() const { return active_; }
    uint64_t framesWritten() const { return framesWritten_.load(); }
    size_t   queueDepth()    const;
    bool     queueDropped()  const { return dropped_.load() > 0; }

    // Path to the most recent recording's directory (empty if never recorded).
    const std::string& lastDir() const { return lastDir_; }

private:
    // 8 PBOs × (up to) 4 concurrent writers leaves 4 queue slots of slack.
    static constexpr int N_PBO     = 8;
    static constexpr int N_WRITERS = 4;

    struct InFlight {
        int     pboIdx;
        GLsync  fence;
        uint64_t frameNum;
    };

    bool        active_ = false;
    GLuint      pbo_[N_PBO] = {0};
    int         srcW_ = 0, srcH_ = 0;
    int         fps_ = 60;
    int         precision_ = 16;     // 16 = half, 32 = float
    int         bytesPerPixel_ = 8;  // 8 for half-RGBA, 16 for float-RGBA
    int         pboCursor_ = 0;
    uint64_t    nextFrameNum_ = 0;
    std::chrono::steady_clock::time_point lastCaptureTime_{};

    std::string outDir_;
    std::string lastDir_;
    FILE*       manifest_ = nullptr;

    std::mutex              mu_;
    std::condition_variable cv_;
    std::deque<InFlight>    queue_;
    bool                    stopFlag_ = false;

    // Tracks PBOs held by writers (popped from queue, not yet unmapped).
    // Capture gates on queue.size() + inFlight >= N_PBO so a PBO is never
    // reused while a writer still has it mapped.
    std::atomic<int>        inFlight_ = 0;

    std::vector<std::thread> writers_;
    std::vector<GLFWwindow*> writerCtxs_;

    std::atomic<uint64_t>   framesWritten_ = 0;
    std::atomic<uint64_t>   dropped_ = 0;

    void writerLoop(int id);
};
