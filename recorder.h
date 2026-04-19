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
//
// Pipeline (three stages, decoupled by queues):
//
//   render thread  →  capture() enqueues (pboIdx, fence)
//
//   N_READBACK readers (shared GL ctx, cheap):
//     wait fence → glMapBuffer → memcpy (+ float→half if precision=32)
//     into a heap buffer from the RAM pool → unmap (PBO freed fast)
//     → enqueue (heapBuf, frameN) on encode queue
//
//   encoderThreads encoders (pure CPU, no GL ctx):
//     exr::write_rgba_half(..., compression) → return buffer to pool
//
// The RAM pool is the main capacity knob. Buffer count = ramBudget / frameBytes.

class GLFWwindow;

class Recorder {
public:
    struct Config {
        // RAM budget for the heap-buffer pool, in MB. 0 = auto
        // (min(availableRAM/4, 8 GB), floored at 256 MB).
        int  ramBudgetMB   = 0;
        // Number of pure-CPU encoder threads. 0 = auto
        // (hardware_concurrency() - 2, clamped to [2, 8]).
        int  encoderThreads = 0;
        // Write uncompressed EXR. Files ~2× larger, writes ~5-10× faster.
        bool uncompressed  = false;
    };

    // precision: 16 or 32. Selects PBO readback format (GL_HALF_FLOAT or GL_FLOAT)
    // so the render thread does not trigger a driver-side format conversion.
    bool start(int srcW, int srcH, int fps, int precision, GLFWwindow* mainWin,
               const Config& cfg);
    void stop();
    void capture(GLuint srcFBO);

    bool active() const { return active_; }
    uint64_t framesWritten() const { return framesWritten_.load(); }
    size_t   queueDepth()    const;
    bool     queueDropped()  const { return dropped_.load() > 0; }

    // Path to the most recent recording's directory (empty if never recorded).
    const std::string& lastDir() const { return lastDir_; }

private:
    static constexpr int N_PBO      = 8;
    static constexpr int N_READBACK = 4;

    struct InFlight {
        int     pboIdx;
        GLsync  fence;
        uint64_t frameNum;
    };

    // A single encode job: a heap buffer from the pool carrying half-float
    // RGBA for one frame, plus its frame index for filename generation.
    struct EncodeJob {
        std::vector<uint16_t>* buf;
        uint64_t frameNum;
    };

    bool        active_ = false;
    GLuint      pbo_[N_PBO] = {0};
    int         srcW_ = 0, srcH_ = 0;
    int         fps_ = 60;
    int         precision_ = 16;     // 16 = half, 32 = float
    int         bytesPerPixel_ = 8;  // 8 for half-RGBA (PBO), 16 for float-RGBA (PBO)
    int         pboCursor_ = 0;
    uint64_t    nextFrameNum_ = 0;
    std::chrono::steady_clock::time_point lastCaptureTime_{};

    std::string outDir_;
    std::string lastDir_;
    FILE*       manifest_ = nullptr;

    // ── PBO → reader queue ────────────────────────────────────────────────
    std::mutex              mu_;
    std::condition_variable cv_;
    std::deque<InFlight>    queue_;
    bool                    stopFlag_ = false;
    std::atomic<int>        inFlight_ = 0;

    std::vector<std::thread> readers_;
    std::vector<GLFWwindow*> readerCtxs_;

    // ── RAM pool: buffers shuttle between readers and encoders ────────────
    std::vector<std::vector<uint16_t>>  bufPool_;       // owns the memory
    std::deque<std::vector<uint16_t>*>  freeBufs_;      // available for readers
    std::deque<EncodeJob>               encodeQueue_;   // ready to write
    std::mutex                          poolMu_;
    std::condition_variable             freeCv_;        // signalled on buf return
    std::condition_variable             encodeCv_;      // signalled on job push
    bool                                encodeStopFlag_ = false;

    std::vector<std::thread> encoders_;
    int                      encoderCount_ = 0;
    bool                     uncompressed_ = false;

    std::atomic<uint64_t>   framesWritten_ = 0;
    std::atomic<uint64_t>   dropped_ = 0;

    void readerLoop(int id);
    void encoderLoop(int id);
};
