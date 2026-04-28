#pragma once
#include <cstdint>
#include <cstddef>

// Platform webcam → RGB buffer for use as a GL texture source.
// The interface stays fixed so main.cpp is portable across backends.

class Camera {
public:
    Camera();
    ~Camera();

    // Open the first available video device at approximately w×h.
    // Returns true on success. If there is no webcam, returns false and the
    // app runs without external input — the External layer simply becomes a no-op.
    bool open(int w, int h);

    // Non-blocking. Fills `rgb` (w*h*3 bytes, top-down) if a new frame is ready.
    // Returns true only when a new frame was actually captured this call.
    bool grab(uint8_t* rgb);

    int width()  const { return w_; }
    int height() const { return h_; }
    bool active() const {
#ifdef _WIN32
        return reader_ != nullptr;
#else
        return impl_ != nullptr;
#endif
    }

private:
    // Windows Media Foundation state. Other platforms currently use a stub.
#ifdef _WIN32
    void* reader_ = nullptr;   // IMFSourceReader*
    int   w_ = 0, h_ = 0;
    uint32_t pixfmt_ = 0;      // fourcc of the negotiated format
    bool     mf_started_ = false;
#else
    void* impl_ = nullptr;     // platform-specific backend object
    int   w_ = 0, h_ = 0;
#endif
};
