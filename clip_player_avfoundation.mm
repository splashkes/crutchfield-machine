// clip_player_avfoundation.mm
// AVFoundation-backed ClipPlayer for macOS. Reads .mp4/.mov/.m4v files
// directly via AVAssetReader, decoding frames on demand into a GL texture.

#include "clip_player.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <GL/glew.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Objective-C backend
// ---------------------------------------------------------------------------

@interface ClipBackend : NSObject {
@public
    std::vector<std::pair<std::string, std::string>> clips; // {stem, path}
    int    current;
    int    lastCurrent;
    double accumTime;

    AVAssetReader* __strong            reader;
    AVAssetReaderTrackOutput* __strong trackOut;
}
@end

@implementation ClipBackend
- (instancetype)init {
    self = [super init];
    if (self) { current = 0; lastCurrent = -1; accumTime = 0.0; }
    return self;
}
@end

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isVideoFile(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v";
}

static void openReader(ClipBackend* b, const std::string& path) {
    b->reader   = nil;
    b->trackOut = nil;

    NSURL* url = [NSURL fileURLWithPath:
        [NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
    if (!asset) {
        fprintf(stderr, "[clip] failed to open: %s\n", path.c_str());
        return;
    }

    NSArray<AVAssetTrack*>* tracks =
        [asset tracksWithMediaType:AVMediaTypeVideo];
    if (!tracks || tracks.count == 0) {
        fprintf(stderr, "[clip] no video track in: %s\n", path.c_str());
        return;
    }

    NSError* err = nil;
    AVAssetReader* reader =
        [AVAssetReader assetReaderWithAsset:asset error:&err];
    if (!reader) {
        fprintf(stderr, "[clip] AVAssetReader failed: %s\n",
                err ? [[err localizedDescription] UTF8String] : "?");
        return;
    }

    NSDictionary* settings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    AVAssetReaderTrackOutput* out =
        [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:tracks[0]
                                                   outputSettings:settings];
    out.alwaysCopiesSampleData = NO;

    if (![reader canAddOutput:out]) {
        fprintf(stderr, "[clip] reader cannot add output\n");
        return;
    }
    [reader addOutput:out];

    if (![reader startReading]) {
        fprintf(stderr, "[clip] startReading failed\n");
        return;
    }

    b->reader   = reader;
    b->trackOut = out;
}

static bool uploadSampleBuffer(CMSampleBufferRef sb, GLuint tex) {
    CVImageBufferRef img = CMSampleBufferGetImageBuffer(sb);
    if (!img) return false;

    CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    const int    w      = (int)CVPixelBufferGetWidth(img);
    const int    h      = (int)CVPixelBufferGetHeight(img);
    const size_t stride = CVPixelBufferGetBytesPerRow(img);
    const void*  src    = CVPixelBufferGetBaseAddress(img);

    if (!src || w <= 0 || h <= 0) {
        CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    if (stride != (size_t)w * 4)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(stride / 4));

    // kCVPixelFormatType_32BGRA: bytes in memory are [B G R A].
    // GL_BGRA + GL_UNSIGNED_BYTE reads them correctly into an RGBA8 texture.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, src);

    if (stride != (size_t)w * 4)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    return true;
}

// ---------------------------------------------------------------------------
// ClipPlayer C++ implementation
// ---------------------------------------------------------------------------

ClipPlayer::ClipPlayer() {
    @autoreleasepool {
        ClipBackend* b = [[ClipBackend alloc] init];
        impl_ = (__bridge_retained void*)b;
    }
}

ClipPlayer::~ClipPlayer() {
    @autoreleasepool {
        ClipBackend* __unused b =
            (__bridge_transfer ClipBackend*)impl_;
        impl_ = nullptr;
    }
}

void ClipPlayer::scan(const std::string& dir) {
    @autoreleasepool {
        ClipBackend* b = (__bridge ClipBackend*)impl_;
        b->reader     = nil;
        b->trackOut   = nil;
        b->clips.clear();
        b->current     = 0;
        b->lastCurrent = -1;
        b->accumTime   = 0.0;

        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            fs::create_directories(dir, ec);
            return;
        }

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (entry.is_regular_file() && isVideoFile(entry.path())) {
                std::string stem = entry.path().stem().string();
                b->clips.push_back({stem, entry.path().string()});
            }
        }

        std::sort(b->clips.begin(), b->clips.end(),
                  [](const auto& a, const auto& x) {
                      return a.first < x.first;
                  });
    }
}

void ClipPlayer::next() {
    @autoreleasepool {
        ClipBackend* b = (__bridge ClipBackend*)impl_;
        if (b->clips.empty()) return;
        b->current   = (b->current + 1) % (int)b->clips.size();
        b->reader    = nil;
        b->trackOut  = nil;
        b->accumTime = 0.0;
    }
}

void ClipPlayer::prev() {
    @autoreleasepool {
        ClipBackend* b = (__bridge ClipBackend*)impl_;
        if (b->clips.empty()) return;
        b->current = (b->current - 1 + (int)b->clips.size())
                      % (int)b->clips.size();
        b->reader    = nil;
        b->trackOut  = nil;
        b->accumTime = 0.0;
    }
}

int ClipPlayer::clipCount() const {
    ClipBackend* b = (__bridge ClipBackend*)impl_;
    return (int)b->clips.size();
}

int ClipPlayer::currentClip() const {
    ClipBackend* b = (__bridge ClipBackend*)impl_;
    return b->current;
}

const std::string& ClipPlayer::clipName() const {
    static const std::string none = "(none)";
    ClipBackend* b = (__bridge ClipBackend*)impl_;
    if (b->clips.empty()) return none;
    return b->clips[b->current].first;
}

bool ClipPlayer::isActive() const {
    ClipBackend* b = (__bridge ClipBackend*)impl_;
    return !b->clips.empty();
}

bool ClipPlayer::update(double dt, GLuint tex) {
    @autoreleasepool {
        ClipBackend* b = (__bridge ClipBackend*)impl_;
        if (b->clips.empty() || tex == 0) return false;

        if (b->current != b->lastCurrent || !b->reader) {
            openReader(b, b->clips[b->current].second);
            b->lastCurrent = b->current;
            b->accumTime   = 0.0;
            if (!b->reader) return false;
        }

        const double frameDur = (fps > 0.0f)
            ? (1.0 / (double)fps) : (1.0 / 24.0);
        b->accumTime += dt;
        if (b->accumTime < frameDur) return false;
        // fmod prevents debt from piling up after a pause or slow frame.
        b->accumTime = std::fmod(b->accumTime, frameDur);

        CMSampleBufferRef sb = [b->trackOut copyNextSampleBuffer];
        if (!sb) {
            if (!loop) return false;
            openReader(b, b->clips[b->current].second);
            if (!b->reader) return false;
            sb = [b->trackOut copyNextSampleBuffer];
            if (!sb) return false;
        }

        bool ok = uploadSampleBuffer(sb, tex);
        CFRelease(sb);
        return ok;
    }
}
