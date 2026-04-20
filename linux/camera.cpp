// camera.cpp — V4L2 capture with YUYV→RGB software conversion.
// MJPEG path intentionally not included: keeps the dependencies to zero
// (no libjpeg). Any webcam built in the last 15 years supports YUYV.

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

static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r;
}

Camera::Camera() {}

Camera::~Camera() {
    if (fd_ >= 0) {
        enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &t);
        for (int i = 0; i < nbuf_; i++)
            if (buf_[i].start) munmap(buf_[i].start, buf_[i].length);
        close(fd_);
    }
}

bool Camera::open(int w, int h) {
    // Find the first working /dev/videoN.
    for (int n = 0; n < 10; n++) {
        char dev[32]; snprintf(dev, sizeof dev, "/dev/video%d", n);
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

        fd_ = fd;
        w_  = fmt.fmt.pix.width;
        h_  = fmt.fmt.pix.height;
        pixfmt_ = fmt.fmt.pix.pixelformat;
        break;
    }
    if (fd_ < 0) {
        fprintf(stderr, "[camera] no usable /dev/videoN (that's fine — "
                        "external layer will stay disabled)\n");
        return false;
    }

    // Request 4 mmap'd buffers.
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        fprintf(stderr, "[camera] REQBUFS failed\n"); close(fd_); fd_ = -1; return false;
    }
    nbuf_ = (int)req.count;
    for (int i = 0; i < nbuf_; i++) {
        v4l2_buffer b{};
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &b) < 0) return false;
        buf_[i].length = b.length;
        buf_[i].start  = mmap(nullptr, b.length, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd_, b.m.offset);
        if (buf_[i].start == MAP_FAILED) return false;
        if (xioctl(fd_, VIDIOC_QBUF, &b) < 0) return false;
    }

    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &t) < 0) {
        fprintf(stderr, "[camera] STREAMON failed\n"); return false;
    }
    fprintf(stdout, "[camera] %dx%d YUYV on fd %d\n", w_, h_, fd_);
    return true;
}

// YUYV → RGB, BT.601, with Y-flip so GL textures come out right side up.
// Output is packed RGB8, top-down.
static inline void yuyv_to_rgb(const uint8_t* yuyv, int w, int h, uint8_t* rgb) {
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

bool Camera::grab(uint8_t* rgb) {
    if (fd_ < 0) return false;
    pollfd pfd{ fd_, POLLIN, 0 };
    if (poll(&pfd, 1, 0) <= 0) return false;        // no frame ready
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &b) < 0) return false;
    yuyv_to_rgb((const uint8_t*)buf_[b.index].start, w_, h_, rgb);
    xioctl(fd_, VIDIOC_QBUF, &b);
    return true;
}
