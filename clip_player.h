#pragma once
#include <string>
#include <GL/glew.h>

// ClipPlayer — scan a directory of video files, play them back into a GL
// texture, and expose clip-navigation controls.
//
// macOS: backed by AVFoundation (AVAssetReader). Drop .mp4/.mov/.m4v files
// directly in clips/:
//   clips/
//     my_loop.mp4
//     other.mov
//     ...
//
// Linux / Windows: PNG/JPEG image-sequence fallback. Each clip is a
// subdirectory of numbered frames (e.g. clips/my_loop/000001.png ...).
// Convert video with: ffmpeg -i clip.mp4 clips/my_loop/%06d.png

class ClipPlayer {
public:
    ClipPlayer();
    ~ClipPlayer();

    // Scan dir for clips. Safe to call multiple times to refresh.
    // Creates the directory if missing.
    void scan(const std::string& dir);

    // Navigate clips. No-op if no clips are loaded.
    void next();
    void prev();

    int  clipCount()   const;
    int  currentClip() const;

    // Display name of the current clip (file stem or folder name), or "(none)".
    const std::string& clipName() const;

    // Advance the frame clock by dt seconds. Uploads a new frame to tex when
    // due. Returns true when a new frame was uploaded.
    // tex must be a valid GL_TEXTURE_2D handle (caller creates / owns it).
    bool update(double dt, GLuint tex);

    // True if at least one clip is loaded.
    bool isActive() const;

    float fps  = 24.0f;  // playback rate in frames per second
    bool  loop = true;   // loop at end-of-stream

private:
    void* impl_ = nullptr;
};
