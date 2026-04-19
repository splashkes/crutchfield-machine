// recorder.cpp — Lossless EXR-sequence recorder.
//
// Pipeline stages, from GPU to disk:
//
//   1. capture() (render thread): glReadPixels → PBO + fence. Push (pbo, fence)
//      onto `queue_`. Drops the frame when the PBO ring is full.
//
//   2. readerLoop (N_READBACK threads, each with a shared GL context):
//      wait fence, map PBO, copy (and if precision=32 convert float→half) into
//      a heap buffer acquired from `freeBufs_`. Unmap, which frees the PBO
//      for immediate reuse. Push the heap buffer onto `encodeQueue_`.
//
//   3. encoderLoop (encoderCount_ pure-CPU threads): pop `encodeQueue_`,
//      exr::write_rgba_half (with chosen compression), return buffer to
//      `freeBufs_`.
//
// Back-pressure is implicit: if encoders can't keep up, `freeBufs_` empties;
// readers block; PBOs back up; capture() drops. The RAM pool size is the
// buffer between bursty capture rate and steady-state write throughput.

#include "recorder.h"
#include "exr_write.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

namespace fs = std::filesystem;

// Resolve Config defaults for this capture session.
static Recorder::Config resolve_config(const Recorder::Config& in,
                                       size_t perFrameBytes) {
    Recorder::Config c = in;

    if (c.ramBudgetMB == 0) {
#ifdef _WIN32
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof m;
        if (GlobalMemoryStatusEx(&m)) {
            uint64_t availMB = (uint64_t)(m.ullAvailPhys / (1024ull * 1024ull));
            uint64_t quarter = availMB / 4;
            uint64_t cap     = 8ull * 1024;  // 8 GB
            uint64_t budget  = quarter < cap ? quarter : cap;
            if (budget < 256) budget = 256;
            c.ramBudgetMB = (int)budget;
        } else {
            c.ramBudgetMB = 1024;
        }
#else
        c.ramBudgetMB = 1024;
#endif
    }
    // Keep a sane minimum — always at least 4 frames.
    size_t minMB = (4 * perFrameBytes + (1u << 20) - 1) >> 20;
    if ((size_t)c.ramBudgetMB < minMB) c.ramBudgetMB = (int)minMB;

    if (c.encoderThreads == 0) {
        unsigned hc = std::thread::hardware_concurrency();
        int n = (int)hc - 2;
        if (n < 2) n = 2;
        if (n > 8) n = 8;
        c.encoderThreads = n;
    }
    return c;
}

bool Recorder::start(int srcW, int srcH, int fps, int precision, GLFWwindow* mainWin,
                     const Config& cfgIn) {
    if (active_) return true;
    srcW_ = srcW; srcH_ = srcH;
    fps_ = fps;
    precision_ = (precision == 32) ? 32 : 16;
    bytesPerPixel_ = (precision_ == 32) ? 16 : 8;  // PBO size (RGBA × 4|2 bytes)

    // Heap-buffer frame size is ALWAYS half-float (8 bytes/px), regardless of
    // PBO precision — readers do the float→half conversion on the way in.
    const size_t halfFrameBytes = (size_t)srcW_ * srcH_ * 8;
    Config cfg = resolve_config(cfgIn, halfFrameBytes);
    uncompressed_ = cfg.uncompressed;
    encoderCount_ = cfg.encoderThreads;

    int nBuffers = (int)(((size_t)cfg.ramBudgetMB * 1024ull * 1024ull) / halfFrameBytes);
    if (nBuffers < 4)    nBuffers = 4;
    if (nBuffers > 4096) nBuffers = 4096;   // sanity cap
    const size_t actualRamMB =
        ((size_t)nBuffers * halfFrameBytes + (1u << 20) - 1) >> 20;

    // Output directory: ./recordings/feedback_YYYYMMDD_HHMMSS/
    char ts[64];
    time_t t = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    strftime(ts, sizeof ts, "feedback_%Y%m%d_%H%M%S", &lt);

    fs::path base = "recordings";
    if (!fs::exists(base)) fs::create_directory(base);
    outDir_ = (base / ts).string();
    fs::create_directory(outDir_);
    lastDir_ = outDir_;

    std::string manPath = (fs::path(outDir_) / "manifest.txt").string();
    manifest_ = std::fopen(manPath.c_str(), "w");
    if (manifest_) {
        char tsHuman[64];
        strftime(tsHuman, sizeof tsHuman, "%Y-%m-%d %H:%M:%S", &lt);
        std::fprintf(manifest_,
            "# video feedback recording manifest\n"
            "format     = exr-sequence\n"
            "resolution = %dx%d\n"
            "pixel_type = half-float RGBA, %s\n"
            "fps        = %d\n"
            "readback   = %s (source precision %d)\n"
            "ram_buffer = %zu MB (%d frames)\n"
            "encoders   = %d threads\n"
            "started    = %s\n"
            "frames     = (updated on stop)\n",
            srcW_, srcH_,
            uncompressed_ ? "no compression" : "ZIP compression",
            fps_,
            precision_ == 32 ? "GL_FLOAT → CPU half" : "GL_HALF_FLOAT direct",
            precision_,
            actualRamMB, nBuffers,
            encoderCount_,
            tsHuman);
        std::fflush(manifest_);
    }

    // PBO ring sized for the native readback type — no GPU-side conversion.
    glGenBuffers(N_PBO, pbo_);
    for (int i = 0; i < N_PBO; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER,
                     (GLsizeiptr)srcW_ * srcH_ * bytesPerPixel_,
                     nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    // Shared GL contexts for readers only; encoders don't touch GL.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    readerCtxs_.assign(N_READBACK, nullptr);
    for (int i = 0; i < N_READBACK; i++) {
        readerCtxs_[i] = glfwCreateWindow(1, 1, "rec_reader_ctx", nullptr, mainWin);
        if (!readerCtxs_[i]) {
            std::fprintf(stderr, "[rec] failed to create shared GL context %d\n", i);
            glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
            for (auto* c : readerCtxs_) if (c) glfwDestroyWindow(c);
            readerCtxs_.clear();
            if (manifest_) { std::fclose(manifest_); manifest_ = nullptr; }
            return false;
        }
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    // Allocate the heap buffer pool. Each buffer holds one half-float RGBA frame.
    const size_t halfFrameElems = (size_t)srcW_ * srcH_ * 4;
    bufPool_.clear();
    bufPool_.reserve(nBuffers);
    freeBufs_.clear();
    for (int i = 0; i < nBuffers; i++) {
        bufPool_.emplace_back(halfFrameElems);
        freeBufs_.push_back(&bufPool_.back());
    }

    pboCursor_ = 0;
    nextFrameNum_ = 0;
    lastCaptureTime_ = {};
    framesWritten_.store(0);
    dropped_.store(0);
    inFlight_.store(0);
    stopFlag_ = false;
    encodeStopFlag_ = false;
    active_ = true;

    readers_.clear();
    readers_.reserve(N_READBACK);
    for (int i = 0; i < N_READBACK; i++)
        readers_.emplace_back(&Recorder::readerLoop, this, i);

    encoders_.clear();
    encoders_.reserve(encoderCount_);
    for (int i = 0; i < encoderCount_; i++)
        encoders_.emplace_back(&Recorder::encoderLoop, this, i);

    std::printf("[rec] started: %s/  (%dx%d EXR @ %d fps, %s readback, %s, "
                "RAM %zu MB / %d frames, %d readers + %d encoders)\n",
                outDir_.c_str(), srcW_, srcH_, fps_,
                precision_ == 32 ? "float→half" : "half direct",
                uncompressed_ ? "uncompressed" : "ZIP",
                actualRamMB, nBuffers,
                N_READBACK, encoderCount_);
    std::fflush(stdout);
    return true;
}

void Recorder::capture(GLuint srcFBO) {
    if (!active_) return;

    // Cadence gate: only capture if at least 1/fps seconds have elapsed
    // since the last accepted frame.
    auto now = std::chrono::steady_clock::now();
    if (lastCaptureTime_.time_since_epoch().count() != 0) {
        auto elapsed = std::chrono::duration<double>(now - lastCaptureTime_).count();
        double period = 1.0 / (double)fps_;
        if (elapsed < period * 0.8) return;
    }

    // Try to take a free PBO slot. If the ring is saturated (all 8 PBOs in
    // flight because readers haven't drained them), drop this frame.
    std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
    if (!lk.owns_lock()) {
        dropped_.fetch_add(1);
        return;
    }
    if ((int)queue_.size() + inFlight_.load() >= N_PBO) {
        lk.unlock();
        dropped_.fetch_add(1);
        return;
    }
    lk.unlock();

    int idx = pboCursor_;
    pboCursor_ = (pboCursor_ + 1) % N_PBO;

    const GLenum readType = (precision_ == 32) ? GL_FLOAT : GL_HALF_FLOAT;
    GLint prevRead = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[idx]);
    glReadPixels(0, 0, srcW_, srcH_, GL_RGBA, readType, (void*)0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);

    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    lastCaptureTime_ = now;

    lk.lock();
    queue_.push_back({idx, fence, nextFrameNum_++});
    cv_.notify_one();
}

void Recorder::readerLoop(int id) {
    glfwMakeContextCurrent(readerCtxs_[id]);

    const size_t halfElems = (size_t)srcW_ * srcH_ * 4;

    while (true) {
        InFlight inflight;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this]{ return !queue_.empty() || stopFlag_; });
            if (queue_.empty() && stopFlag_) break;
            inflight = queue_.front();
            queue_.pop_front();
            inFlight_.fetch_add(1);
        }

        // Acquire a free heap buffer. If the pool is empty, wait — we hold the
        // PBO in inFlight_ meanwhile, which back-pressures capture() via the
        // PBO ring. If the recorder is stopping and no buffers ever come,
        // fall back to discarding this frame.
        std::vector<uint16_t>* dst = nullptr;
        {
            std::unique_lock<std::mutex> pk(poolMu_);
            freeCv_.wait(pk, [this]{ return !freeBufs_.empty() || stopFlag_; });
            if (!freeBufs_.empty()) {
                dst = freeBufs_.front();
                freeBufs_.pop_front();
            }
        }
        if (!dst) {
            glDeleteSync(inflight.fence);
            inFlight_.fetch_sub(1);
            dropped_.fetch_add(1);
            continue;
        }

        // Wait for the GPU readback to complete. Blocks this reader only.
        GLenum waitResult;
        do {
            waitResult = glClientWaitSync(inflight.fence,
                                          GL_SYNC_FLUSH_COMMANDS_BIT,
                                          1'000'000'000ULL);
        } while (waitResult == GL_TIMEOUT_EXPIRED);
        glDeleteSync(inflight.fence);
        if (waitResult == GL_WAIT_FAILED) {
            inFlight_.fetch_sub(1);
            dropped_.fetch_add(1);
            // Return the unused buffer.
            { std::lock_guard<std::mutex> pk(poolMu_); freeBufs_.push_back(dst); }
            freeCv_.notify_one();
            continue;
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[inflight.pboIdx]);
        void* p = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (!p) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            inFlight_.fetch_sub(1);
            dropped_.fetch_add(1);
            { std::lock_guard<std::mutex> pk(poolMu_); freeBufs_.push_back(dst); }
            freeCv_.notify_one();
            continue;
        }

        if (precision_ == 32) {
            const float* fsrc = (const float*)p;
            uint16_t*    hdst = dst->data();
            for (size_t i = 0; i < halfElems; i++)
                hdst[i] = exr::f32_to_f16(fsrc[i]);
        } else {
            std::memcpy(dst->data(), p, halfElems * sizeof(uint16_t));
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        inFlight_.fetch_sub(1);

        // Hand off to encoders.
        {
            std::lock_guard<std::mutex> pk(poolMu_);
            encodeQueue_.push_back({dst, inflight.frameNum});
        }
        encodeCv_.notify_one();
    }

    glfwMakeContextCurrent(nullptr);
}

void Recorder::encoderLoop(int /*id*/) {
    const exr::Compression comp =
        uncompressed_ ? exr::Compression::NONE : exr::Compression::ZIP;

    while (true) {
        EncodeJob job;
        {
            std::unique_lock<std::mutex> pk(poolMu_);
            encodeCv_.wait(pk, [this]{
                return !encodeQueue_.empty() || encodeStopFlag_;
            });
            if (encodeQueue_.empty() && encodeStopFlag_) break;
            job = encodeQueue_.front();
            encodeQueue_.pop_front();
        }

        char fname[64];
        std::snprintf(fname, sizeof fname, "frame_%06llu.exr",
                      (unsigned long long)job.frameNum);
        std::string fullPath = (fs::path(outDir_) / fname).string();
        if (exr::write_rgba_half(fullPath.c_str(), srcW_, srcH_,
                                 job.buf->data(), comp)) {
            framesWritten_.fetch_add(1);
        } else {
            dropped_.fetch_add(1);
        }

        // Return buffer to the pool.
        {
            std::lock_guard<std::mutex> pk(poolMu_);
            freeBufs_.push_back(job.buf);
        }
        freeCv_.notify_one();
    }
}

size_t Recorder::queueDepth() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
    return queue_.size();
}

void Recorder::stop() {
    if (!active_) return;

    // 1) Tell readers to drain the PBO queue and stop. They'll push every
    //    remaining in-flight frame to encodeQueue_ before exiting.
    {
        std::lock_guard<std::mutex> lk(mu_);
        stopFlag_ = true;
        cv_.notify_all();
    }
    // Also wake any readers waiting on free buffers — they're gated by
    // stopFlag_ too, and if encoders have drained nothing is coming.
    freeCv_.notify_all();
    for (auto& t : readers_) if (t.joinable()) t.join();
    readers_.clear();

    // 2) All pending PBO work has landed in encodeQueue_. Tell encoders to
    //    finish draining it and exit.
    {
        std::lock_guard<std::mutex> pk(poolMu_);
        encodeStopFlag_ = true;
    }
    encodeCv_.notify_all();
    for (auto& t : encoders_) if (t.joinable()) t.join();
    encoders_.clear();

    // 3) GL resources and pool cleanup.
    glDeleteBuffers(N_PBO, pbo_);
    for (int i = 0; i < N_PBO; i++) pbo_[i] = 0;

    for (auto* c : readerCtxs_) if (c) glfwDestroyWindow(c);
    readerCtxs_.clear();

    freeBufs_.clear();
    bufPool_.clear();
    encodeQueue_.clear();

    uint64_t wrote = framesWritten_.load();
    uint64_t drops = dropped_.load();
    if (manifest_) {
        std::fprintf(manifest_, "frames_written = %llu\n", (unsigned long long)wrote);
        if (drops) std::fprintf(manifest_, "frames_dropped = %llu\n",
                                (unsigned long long)drops);
        std::fclose(manifest_);
        manifest_ = nullptr;
    }

    std::printf("[rec] stopped — wrote %llu frames to %s/",
                (unsigned long long)wrote, outDir_.c_str());
    if (drops > 0) std::printf("  (%llu DROPPED)", (unsigned long long)drops);
    std::printf("\n");
    std::fflush(stdout);

    active_ = false;
}
