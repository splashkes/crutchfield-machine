// clip_player.cpp
// PNG/JPEG image-sequence fallback for Linux and Windows (no AVFoundation).
// macOS uses clip_player_avfoundation.mm instead.

#include "clip_player.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

namespace fs = std::filesystem;

static bool is_image(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

// ---------------------------------------------------------------------------
// Opaque implementation struct
// ---------------------------------------------------------------------------

struct ClipPlayerImpl {
    struct Clip {
        std::string              name;
        std::vector<std::string> frames; // sorted file paths
    };
    std::vector<Clip> clips;
    int    current   = 0;
    int    frameIdx  = 0;
    double accumTime = 0.0;
    int    lastFrame = -1;
    int    lastClip  = -1;
};

static bool loadFramePNG(const std::string& path, GLuint tex) {
    int w = 0, h = 0, ch = 0;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &ch, 3);
    if (!data) {
        fprintf(stderr, "[clip] stbi_load failed: %s\n", path.c_str());
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// ClipPlayer
// ---------------------------------------------------------------------------

ClipPlayer::ClipPlayer()  { impl_ = new ClipPlayerImpl(); }
ClipPlayer::~ClipPlayer() { delete static_cast<ClipPlayerImpl*>(impl_); }

void ClipPlayer::scan(const std::string& dir) {
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    im->clips.clear();
    im->current = im->frameIdx = 0;
    im->accumTime = 0.0;
    im->lastFrame = im->lastClip = -1;

    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        return;
    }

    std::vector<ClipPlayerImpl::Clip> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_directory()) {
            ClipPlayerImpl::Clip clip;
            clip.name = entry.path().filename().string();
            std::error_code ec2;
            for (const auto& f : fs::directory_iterator(entry.path(), ec2))
                if (f.is_regular_file() && is_image(f.path()))
                    clip.frames.push_back(f.path().string());
            if (!clip.frames.empty()) {
                std::sort(clip.frames.begin(), clip.frames.end());
                found.push_back(std::move(clip));
            }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    im->clips = std::move(found);
}

void ClipPlayer::next() {
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    if (im->clips.empty()) return;
    im->current = (im->current + 1) % (int)im->clips.size();
    im->frameIdx = 0; im->accumTime = 0.0; im->lastFrame = -1;
}

void ClipPlayer::prev() {
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    if (im->clips.empty()) return;
    im->current = (im->current - 1 + (int)im->clips.size())
                   % (int)im->clips.size();
    im->frameIdx = 0; im->accumTime = 0.0; im->lastFrame = -1;
}

int ClipPlayer::clipCount() const {
    return (int)static_cast<ClipPlayerImpl*>(impl_)->clips.size();
}

int ClipPlayer::currentClip() const {
    return static_cast<ClipPlayerImpl*>(impl_)->current;
}

const std::string& ClipPlayer::clipName() const {
    static const std::string none = "(none)";
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    if (im->clips.empty()) return none;
    return im->clips[im->current].name;
}

const std::string& ClipPlayer::clipNameAt(int i) const {
    static const std::string none = "(none)";
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    if (i < 0 || i >= (int)im->clips.size()) return none;
    return im->clips[i].name;
}

bool ClipPlayer::isActive() const {
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    return !im->clips.empty() && !im->clips[im->current].frames.empty();
}

bool ClipPlayer::update(double dt, GLuint tex) {
    auto* im = static_cast<ClipPlayerImpl*>(impl_);
    if (!isActive() || tex == 0) return false;

    auto& clip   = im->clips[im->current];
    int   nFrames = (int)clip.frames.size();

    const double frameDur = (fps > 0.0f) ? (1.0 / fps) : (1.0 / 24.0);
    im->accumTime += dt;
    while (im->accumTime >= frameDur) {
        im->accumTime -= frameDur;
        im->frameIdx++;
        if (im->frameIdx >= nFrames)
            im->frameIdx = loop ? 0 : nFrames - 1;
    }

    if (im->frameIdx == im->lastFrame && im->current == im->lastClip)
        return false;

    if (loadFramePNG(clip.frames[im->frameIdx], tex)) {
        im->lastFrame = im->frameIdx;
        im->lastClip  = im->current;
        return true;
    }
    return false;
}
