#pragma once
#include <cstdint>
#include <cstddef>

// Minimal V4L2 capture → RGB buffer, for use as a GL texture source.
// Only does what's needed for feedback-as-art: first available device,
// YUYV or MJPEG→RGB (whichever the device offers), fixed resolution,
// non-blocking read.

class Camera {
public:
    Camera();
    ~Camera();

    // Open /dev/videoN (N chosen automatically) at roughly w×h.
    // Returns true on success. Safe to call on systems with no camera —
    // just returns false and the app runs without external input.
    bool open(int w, int h);

    // Call every frame. Fills `rgb` (w*h*3 bytes, row-major, top-down).
    // Returns true if a new frame was captured this call; false means
    // no new frame is ready yet (keep using the previous one).
    bool grab(uint8_t* rgb);

    int width()  const { return w_; }
    int height() const { return h_; }
    bool active() const { return fd_ >= 0; }

private:
    int  fd_ = -1;
    int  w_ = 0, h_ = 0;
    uint32_t pixfmt_ = 0;
    struct Buf { void* start; size_t length; };
    Buf  buf_[4];
    int  nbuf_ = 0;
};
