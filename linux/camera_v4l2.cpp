// camera_v4l2.cpp — V4L2 capture with YUYV→RGB software conversion.
//
// All backend state lives in a struct behind the opaque Camera::impl_ pointer,
// matching how camera_avfoundation.mm organises AVFoundation state. The public
// Camera API stays identical to the Windows (Media Foundation) build.

#include "camera.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

namespace {

struct Buffer {
    void*  start  = nullptr;
    size_t length = 0;
};

struct V4L2Impl {
    int      fd       = -1;
    int      nbuf     = 0;
    uint32_t pixfmt   = 0;
    Buffer   buf[8]{};
};

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r;
}

// YUYV → RGB, BT.601. Output is packed RGB8, top-down (GL textures come out
// right side up when uploaded with the usual unpack alignment).
inline void yuyv_to_rgb(const uint8_t* yuyv, int w, int h, uint8_t* rgb) {
    for (int y = 0; y < h; y++) {
        const uint8_t* src = yuyv + y * w * 2;
        uint8_t* dst = rgb + y * w * 3;
        for (int x = 0; x < w; x += 2) {
            int Y0 = src[0], U = src[1], Y1 = src[2], V = src[3];
            int c0 = Y0 - 16, c1 = Y1 - 16, d = U - 128, e = V - 128;
            auto clip = [](int v){ return v<0?0:v>255?255:v; };
            int r0 = clip((298*c0 + 409*e + 128) >> 8);
            int g0 = clip((298*c0 - 100*d - 208*e + 128) >> 8);
            int b0 = clip((298*c0 + 516*d + 128) >> 8);
            int r1 = clip((298*c1 + 409*e + 128) >> 8);
            int g1 = clip((298*c1 - 100*d - 208*e + 128) >> 8);
            int b1 = clip((298*c1 + 516*d + 128) >> 8);
            dst[0]=r0; dst[1]=g0; dst[2]=b0;
            dst[3]=r1; dst[4]=g1; dst[5]=b1;
            src += 4; dst += 6;
        }
    }
}

void teardown(V4L2Impl* im) {
    if (!im) return;
    if (im->fd >= 0) {
        enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(im->fd, VIDIOC_STREAMOFF, &t);
        for (int i = 0; i < im->nbuf; i++)
            if (im->buf[i].start) munmap(im->buf[i].start, im->buf[i].length);
        close(im->fd);
    }
    delete im;
}

} // namespace

Camera::Camera() {}

Camera::~Camera() {
    teardown(static_cast<V4L2Impl*>(impl_));
    impl_ = nullptr;
}

bool Camera::open(int w, int h) {
    V4L2Impl* im = new V4L2Impl();

    // Find the first working /dev/videoN that can serve YUYV at the requested size.
    for (int n = 0; n < 10; n++) {
        char dev[32]; std::snprintf(dev, sizeof dev, "/dev/video%d", n);
        int fd = ::open(dev, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        v4l2_capability cap{};
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 ||
            !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            close(fd); continue;
        }

        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width  = w;
        fmt.fmt.pix.height = h;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { close(fd); continue; }
        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) { close(fd); continue; }

        im->fd     = fd;
        w_         = fmt.fmt.pix.width;
        h_         = fmt.fmt.pix.height;
        im->pixfmt = fmt.fmt.pix.pixelformat;
        break;
    }
    if (im->fd < 0) {
        std::fprintf(stderr, "[camera] no usable /dev/videoN (that's fine — "
                             "external layer will stay disabled)\n");
        delete im;
        return false;
    }

    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(im->fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        std::fprintf(stderr, "[camera] REQBUFS failed\n");
        teardown(im);
        return false;
    }
    im->nbuf = (int)req.count;
    if (im->nbuf > 8) im->nbuf = 8;  // matches Impl.buf[8]
    for (int i = 0; i < im->nbuf; i++) {
        v4l2_buffer b{};
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;
        if (xioctl(im->fd, VIDIOC_QUERYBUF, &b) < 0) { teardown(im); return false; }
        im->buf[i].length = b.length;
        im->buf[i].start  = mmap(nullptr, b.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, im->fd, b.m.offset);
        if (im->buf[i].start == MAP_FAILED) { teardown(im); return false; }
        if (xioctl(im->fd, VIDIOC_QBUF, &b) < 0) { teardown(im); return false; }
    }

    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(im->fd, VIDIOC_STREAMON, &t) < 0) {
        std::fprintf(stderr, "[camera] STREAMON failed\n");
        teardown(im);
        return false;
    }

    impl_ = im;
    std::fprintf(stdout, "[camera] %dx%d YUYV on fd %d\n", w_, h_, im->fd);
    return true;
}

bool Camera::grab(uint8_t* rgb) {
    V4L2Impl* im = static_cast<V4L2Impl*>(impl_);
    if (!im || im->fd < 0) return false;

    pollfd pfd{ im->fd, POLLIN, 0 };
    if (poll(&pfd, 1, 0) <= 0) return false;  // no frame ready

    v4l2_buffer b{};
    b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    if (xioctl(im->fd, VIDIOC_DQBUF, &b) < 0) return false;

    yuyv_to_rgb((const uint8_t*)im->buf[b.index].start, w_, h_, rgb);

    xioctl(im->fd, VIDIOC_QBUF, &b);
    return true;
}
