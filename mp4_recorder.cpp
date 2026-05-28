// mp4_recorder.cpp — popen-backed HEVC recorder.
//
// We keep the design deliberately small: spin up ffmpeg with a raw-RGB
// stdin pipe, push frames through a 2-PBO async readback ring, close
// the pipe to finalize. No background thread, no separate context —
// the readback latency is masked by the PBO ring and the encoder runs
// in another process so its CPU/GPU cost doesn't compete with the
// feedback render thread.

#include "mp4_recorder.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {

// Pick the first ffmpeg binary we can stat. Allows brew installs to
// "just work" without putting them on the GUI app's PATH.
std::string resolveFfmpeg(const std::string& override) {
    if (!override.empty()) return override;
    const char* candidates[] = {
        "/opt/homebrew/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        "/usr/bin/ffmpeg",
        nullptr,
    };
    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && (st.st_mode & S_IXUSR)) {
            return candidates[i];
        }
    }
    return "ffmpeg";  // pray it's on PATH
}

// Build ~/Movies/CrutchfieldMachine (or the user override) and mkdir -p it.
std::string resolveOutDir(const std::string& override) {
    std::string dir = override;
    if (dir.empty()) {
        const char* home = getenv("HOME");
        dir = std::string(home ? home : ".") + "/Movies/CrutchfieldMachine";
    }
    // Walk components and mkdir each. Cheap; runs once per start().
    std::string acc;
    for (size_t i = 0; i <= dir.size(); i++) {
        if (i == dir.size() || dir[i] == '/') {
            if (!acc.empty()) {
                mkdir(acc.c_str(), 0755);
            }
        }
        if (i < dir.size()) acc += dir[i];
    }
    return dir;
}

std::string timestamp() {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d_%H-%M-%S", &tm);
    return buf;
}

// Single source of truth for "what codec presets do we ship". Adding a
// new one means a row here plus a case in buildFfmpegArgs() — both stay
// next to each other so it's hard to drift.
struct CodecPreset {
    const char* name;        // user-facing
    const char* ext;         // ".mp4" / ".mov"
};
const CodecPreset kPresets[] = {
    { "hevc",   ".mp4" },
    { "h264",   ".mp4" },
    { "prores", ".mov" },
};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

int presetIndex(const std::string& name) {
    for (int i = 0; i < kPresetCount; i++) {
        if (name == kPresets[i].name) return i;
    }
    return 0;  // fall back to hevc
}

// Build the argv vector for ffmpeg. Returned as a shell-quoted string
// because popen takes a command line, not an argv array. Width/height
// come from the feedback display; everything else lives in cfg.
std::string buildFfmpegCmd(const std::string& ffmpeg,
                           int w, int h, int fps,
                           const Mp4Recorder::Config& cfg,
                           const std::string& outPath) {
    char buf[1024];
    const char* codecArgs = "";
    if (cfg.codec == "h264") {
        snprintf(buf, sizeof buf,
                 "-c:v h264_videotoolbox -q:v %d -tag:v avc1 "
                 "-pix_fmt yuv420p -movflags +faststart",
                 cfg.quality);
        codecArgs = buf;
    } else if (cfg.codec == "prores") {
        // ProRes 422 HQ (profile 3) via VideoToolbox. Goes in .mov.
        // No quality knob — the profile dictates the bitrate ladder.
        snprintf(buf, sizeof buf,
                 "-c:v prores_videotoolbox -profile:v 3 -pix_fmt yuv422p10le");
        codecArgs = buf;
    } else {
        // Default HEVC (h265) via VideoToolbox. `hvc1` tag makes
        // QuickTime + Finder Preview play it without re-mux.
        snprintf(buf, sizeof buf,
                 "-c:v hevc_videotoolbox -q:v %d -tag:v hvc1 "
                 "-pix_fmt yuv420p -movflags +faststart",
                 cfg.quality);
        codecArgs = buf;
    }
    char cmd[2048];
    snprintf(cmd, sizeof cmd,
        "%s -hide_banner -loglevel warning -y "
        "-f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
        "%s "
        "%s 2>&1",
        ffmpeg.c_str(),
        w, h, fps,
        codecArgs,
        outPath.c_str());
    return cmd;
}

} // namespace

void Mp4Recorder::cycleCodec() {
    if (active_) return;  // can't change mid-recording — stop first
    int idx = presetIndex(cfg_.codec);
    idx = (idx + 1) % kPresetCount;
    cfg_.codec = kPresets[idx].name;
}

bool Mp4Recorder::start(int srcW, int srcH, int fps, const Config& cfg) {
    if (active_) return true;
    if (srcW <= 0 || srcH <= 0) return false;
    cfg_ = cfg;
    w_ = srcW;
    h_ = srcH;
    frameNum_ = 0;
    framesWritten_ = 0;

    std::string ffmpeg = resolveFfmpeg(cfg.ffmpegPath);
    std::string dir    = resolveOutDir(cfg.outDir);
    int idx = presetIndex(cfg_.codec);
    std::string path   = dir + "/crutch-" + timestamp() + kPresets[idx].ext;

    std::string cmd = buildFfmpegCmd(ffmpeg, w_, h_, fps, cfg_, path);
    pipe_ = popen(cmd.c_str(), "w");
    if (!pipe_) {
        fprintf(stderr, "[mp4] popen failed for ffmpeg — recording aborted\n");
        return false;
    }
    // Disable stdio buffering so frame writes go straight down the pipe;
    // any held bytes here would just add latency before ffmpeg sees them.
    setbuf(pipe_, nullptr);

    // PBO ring for async readback. RGB8 keeps the per-frame cost low
    // and matches what we feed ffmpeg without a CPU-side format pass.
    glGenBuffers(N_PBO, pbo_);
    const size_t bytes = (size_t)w_ * (size_t)h_ * 3;
    for (int i = 0; i < N_PBO; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    lastFile_ = path;
    active_ = true;
    fprintf(stderr, "[mp4] recording → %s  (%dx%d @ %d fps, %s)\n",
            path.c_str(), w_, h_, fps, cfg_.codec.c_str());
    return true;
}

void Mp4Recorder::shutdownPipe() {
    if (pipe_) {
        // Closing stdin tells ffmpeg the stream is done; pclose waits
        // for the muxer to write the moov atom and exit cleanly.
        int rc = pclose(pipe_);
        pipe_ = nullptr;
        if (rc != 0) {
            fprintf(stderr, "[mp4] ffmpeg exited with %d (check %s)\n",
                    rc, lastFile_.c_str());
        }
    }
    if (pbo_[0]) {
        glDeleteBuffers(N_PBO, pbo_);
        for (int i = 0; i < N_PBO; i++) pbo_[i] = 0;
    }
}

void Mp4Recorder::stop() {
    if (!active_) return;
    // Drain the PBO ring before closing: any frame that's already in a
    // PBO has been captured but not yet written, so without this we'd
    // lose the last 2 frames of every clip.
    for (int i = 0; i < N_PBO; i++) {
        int idx = (pboCursor_ + i) % N_PBO;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[idx]);
        void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                   (GLsizeiptr)w_ * h_ * 3, GL_MAP_READ_BIT);
        if (p && pipe_) {
            fwrite(p, 1, (size_t)w_ * h_ * 3, pipe_);
            framesWritten_++;
        }
        if (p) glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    shutdownPipe();
    active_ = false;
    fprintf(stderr, "[mp4] stopped — wrote %llu frames to %s\n",
            (unsigned long long)framesWritten_, lastFile_.c_str());
}

void Mp4Recorder::capture(GLuint srcFBO) {
    if (!active_ || !pipe_) return;

    // Bind the source FBO for read. Flip the read on Y so ffmpeg sees
    // top-down rows (GL is bottom-up). We do it by reading directly with
    // the same orientation it was rendered in and let ffmpeg's vflip
    // happen via the source-side cooperation: actually simplest is to
    // pass the rows in GL order and tell ffmpeg there's no flip — the
    // result on disk matches what's on screen.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    // Kick async readback into the current PBO slot.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[pboCursor_]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w_, h_, GL_RGB, GL_UNSIGNED_BYTE, (void*)0);

    // Map the OLDEST PBO (N_PBO frames behind) — that one is guaranteed
    // to have finished. This is what hides the readback latency.
    int oldest = (pboCursor_ + 1) % N_PBO;
    if (frameNum_ >= (uint64_t)(N_PBO - 1)) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[oldest]);
        void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                   (GLsizeiptr)w_ * h_ * 3, GL_MAP_READ_BIT);
        if (p) {
            // OpenGL gives us rows bottom-up; ffmpeg expects top-down.
            // Cheap memcpy with a row-reverse loop.
            const size_t rowBytes = (size_t)w_ * 3;
            const uint8_t* src = (const uint8_t*)p;
            for (int y = h_ - 1; y >= 0; y--) {
                fwrite(src + (size_t)y * rowBytes, 1, rowBytes, pipe_);
            }
            framesWritten_++;
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    pboCursor_ = (pboCursor_ + 1) % N_PBO;
    frameNum_++;
}
