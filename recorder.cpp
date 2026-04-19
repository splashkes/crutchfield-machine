// recorder.cpp — Lossless EXR-sequence recorder via async PBO + fences.

#include "recorder.h"
#include "exr_write.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

bool Recorder::start(int srcW, int srcH, int fps, int precision, GLFWwindow* mainWin) {
    if (active_) return true;
    srcW_ = srcW; srcH_ = srcH;
    fps_ = fps;
    precision_ = (precision == 32) ? 32 : 16;
    bytesPerPixel_ = (precision_ == 32) ? 16 : 8;  // RGBA × 4|2 bytes

    // Create output directory: ./recordings/feedback_YYYYMMDD_HHMMSS/
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

    // Manifest captures resolution and intended fps so post-process tools
    // (and the encode-on-exit prompt) know how to interpret the sequence.
    std::string manPath = (fs::path(outDir_) / "manifest.txt").string();
    manifest_ = std::fopen(manPath.c_str(), "w");
    if (manifest_) {
        char tsHuman[64];
        strftime(tsHuman, sizeof tsHuman, "%Y-%m-%d %H:%M:%S", &lt);
        std::fprintf(manifest_,
            "# video feedback recording manifest\n"
            "format     = exr-sequence\n"
            "resolution = %dx%d\n"
            "pixel_type = half-float RGBA, ZIP compression\n"
            "fps        = %d\n"
            "readback   = %s (source precision %d)\n"
            "started    = %s\n"
            "frames     = (updated on stop)\n",
            srcW_, srcH_, fps_,
            precision_ == 32 ? "GL_FLOAT → CPU half" : "GL_HALF_FLOAT direct",
            precision_, tsHuman);
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

    // Shared GL contexts — one per writer — so map/memcpy runs in parallel.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    writerCtxs_.assign(N_WRITERS, nullptr);
    for (int i = 0; i < N_WRITERS; i++) {
        writerCtxs_[i] = glfwCreateWindow(1, 1, "rec_writer_ctx", nullptr, mainWin);
        if (!writerCtxs_[i]) {
            std::fprintf(stderr, "[rec] failed to create shared GL context %d\n", i);
            glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
            for (auto* c : writerCtxs_) if (c) glfwDestroyWindow(c);
            writerCtxs_.clear();
            if (manifest_) { std::fclose(manifest_); manifest_ = nullptr; }
            return false;
        }
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    pboCursor_ = 0;
    nextFrameNum_ = 0;
    lastCaptureTime_ = {};
    framesWritten_.store(0);
    dropped_.store(0);
    inFlight_.store(0);
    stopFlag_ = false;
    active_ = true;

    writers_.clear();
    writers_.reserve(N_WRITERS);
    for (int i = 0; i < N_WRITERS; i++)
        writers_.emplace_back(&Recorder::writerLoop, this, i);

    std::printf("[rec] started: %s/  (%dx%d EXR @ %d fps, %s readback, %d writers)\n",
                outDir_.c_str(), srcW_, srcH_, fps_,
                precision_ == 32 ? "float→half" : "half direct",
                N_WRITERS);
    std::fflush(stdout);
    return true;
}

void Recorder::capture(GLuint srcFBO) {
    if (!active_) return;

    // Cadence gate: only capture if at least 1/fps seconds have elapsed
    // since the last accepted frame. This decouples capture rate from
    // render rate — render can run at 200fps while we capture at 60,
    // or render at 45fps and we capture at 45 (whichever is slower).
    auto now = std::chrono::steady_clock::now();
    if (lastCaptureTime_.time_since_epoch().count() != 0) {
        auto elapsed = std::chrono::duration<double>(now - lastCaptureTime_).count();
        double period = 1.0 / (double)fps_;
        // Allow a small slack (80% of period) so we don't miss frames due to
        // render-thread jitter. Bias toward capturing rather than skipping.
        if (elapsed < period * 0.8) return;
    }

    // Trylock the queue: if a writer is mid-dequeue, we skip this
    // frame entirely rather than blocking the render thread.
    std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
    if (!lk.owns_lock()) {
        dropped_.fetch_add(1);
        return;
    }
    // A PBO can only be reused once no writer still holds it mapped, so
    // count both queued entries and in-flight (popped-but-not-unmapped) ones.
    if ((int)queue_.size() + inFlight_.load() >= N_PBO) {
        lk.unlock();
        dropped_.fetch_add(1);
        return;
    }
    lk.unlock();   // release before GL calls; we'll re-lock to enqueue

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

void Recorder::writerLoop(int id) {
    glfwMakeContextCurrent(writerCtxs_[id]);

    const size_t px = (size_t)srcW_ * srcH_;
    // Staging buffer sized by source precision. For 32F sources we also need
    // a half-buffer because exr::write_rgba_half takes half-float input.
    std::vector<uint16_t> halfStaging(px * 4);
    std::vector<float>    floatStaging(precision_ == 32 ? px * 4 : 0);

    while (true) {
        InFlight inflight;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this]{ return !queue_.empty() || stopFlag_; });
            if (queue_.empty() && stopFlag_) break;
            inflight = queue_.front();
            queue_.pop_front();
            // Mark this PBO as held by a writer so capture() won't reuse it.
            inFlight_.fetch_add(1);
        }

        // Wait for the GPU readback to complete. Blocks this writer only.
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
            continue;
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[inflight.pboIdx]);
        void* p = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (!p) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            inFlight_.fetch_sub(1);
            dropped_.fetch_add(1);
            continue;
        }
        if (precision_ == 32) {
            std::memcpy(floatStaging.data(), p, floatStaging.size() * sizeof(float));
        } else {
            std::memcpy(halfStaging.data(), p, halfStaging.size() * sizeof(uint16_t));
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        // PBO is released from this writer's perspective — capture may reuse it.
        inFlight_.fetch_sub(1);

        // CPU-side float→half conversion only when source is 32F.
        if (precision_ == 32) {
            const float* fsrc = floatStaging.data();
            uint16_t*    hdst = halfStaging.data();
            const size_t n = px * 4;
            for (size_t i = 0; i < n; i++) hdst[i] = exr::f32_to_f16(fsrc[i]);
        }

        char fname[64];
        std::snprintf(fname, sizeof fname, "frame_%06llu.exr",
                      (unsigned long long)inflight.frameNum);
        std::string fullPath = (fs::path(outDir_) / fname).string();
        if (exr::write_rgba_half(fullPath.c_str(), srcW_, srcH_, halfStaging.data())) {
            framesWritten_.fetch_add(1);
        } else {
            dropped_.fetch_add(1);
        }
    }

    glfwMakeContextCurrent(nullptr);
}

size_t Recorder::queueDepth() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
    return queue_.size();
}

void Recorder::stop() {
    if (!active_) return;

    {
        std::lock_guard<std::mutex> lk(mu_);
        stopFlag_ = true;
        cv_.notify_all();
    }
    for (auto& t : writers_) if (t.joinable()) t.join();
    writers_.clear();

    glDeleteBuffers(N_PBO, pbo_);
    for (int i = 0; i < N_PBO; i++) pbo_[i] = 0;

    for (auto* c : writerCtxs_) if (c) glfwDestroyWindow(c);
    writerCtxs_.clear();

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
