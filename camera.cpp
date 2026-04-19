// camera.cpp — Media Foundation webcam capture on Windows.
// Supports NV12 and YUY2 (virtually every webcam exposes one of these).
//
// No external deps beyond the Windows SDK. Link mfplat.lib mfreadwrite.lib
// mf.lib mfuuid.lib.

#include "camera.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;

// Fourccs (Media Foundation uses GUIDs but the first 4 bytes are fourcc)
static const uint32_t FOURCC_NV12  = 0x3231564E; // 'NV12'
static const uint32_t FOURCC_YUY2  = 0x32595559; // 'YUY2'
static const uint32_t FOURCC_RGB24 = 0x00000014; // MFVideoFormat_RGB24 Data1

static inline int clip8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// BT.601 conversion helpers.
static inline void yuv_to_rgb(int Y, int U, int V, uint8_t* dst) {
    int c = Y - 16, d = U - 128, e = V - 128;
    dst[0] = (uint8_t)clip8((298*c + 409*e + 128) >> 8);
    dst[1] = (uint8_t)clip8((298*c - 100*d - 208*e + 128) >> 8);
    dst[2] = (uint8_t)clip8((298*c + 516*d + 128) >> 8);
}

static void nv12_to_rgb(const uint8_t* src, int stride,
                        int w, int h, uint8_t* rgb) {
    const uint8_t* Yp = src;
    const uint8_t* UV = src + stride * h;
    for (int y = 0; y < h; y++) {
        const uint8_t* yr  = Yp + y * stride;
        const uint8_t* uvr = UV + (y / 2) * stride;
        uint8_t* dst = rgb + y * w * 3;
        for (int x = 0; x < w; x++) {
            int U = uvr[(x / 2) * 2];
            int V = uvr[(x / 2) * 2 + 1];
            yuv_to_rgb(yr[x], U, V, dst + x * 3);
        }
    }
}

static void yuy2_to_rgb(const uint8_t* src, int stride,
                        int w, int h, uint8_t* rgb) {
    for (int y = 0; y < h; y++) {
        const uint8_t* sr = src + y * stride;
        uint8_t* dst = rgb + y * w * 3;
        for (int x = 0; x < w; x += 2) {
            int Y0 = sr[0], U = sr[1], Y1 = sr[2], V = sr[3];
            yuv_to_rgb(Y0, U, V, dst); dst += 3;
            yuv_to_rgb(Y1, U, V, dst); dst += 3;
            sr += 4;
        }
    }
}

Camera::Camera() {}

Camera::~Camera() {
    if (reader_) { ((IMFSourceReader*)reader_)->Release(); reader_ = nullptr; }
    if (mf_started_) { MFShutdown(); CoUninitialize(); }
}

bool Camera::open(int w, int h) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return false;
    if (FAILED(MFStartup(MF_VERSION))) { CoUninitialize(); return false; }
    mf_started_ = true;

    // Enumerate video capture devices — first one wins.
    ComPtr<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return false;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr; UINT32 ndev = 0;
    if (FAILED(MFEnumDeviceSources(attr.Get(), &devs, &ndev)) || ndev == 0) {
        if (devs) CoTaskMemFree(devs);
        fprintf(stderr, "[camera] no webcam found (fine — External layer stays disabled)\n");
        return false;
    }

    ComPtr<IMFMediaSource> source;
    HRESULT hr = devs[0]->ActivateObject(IID_PPV_ARGS(&source));
    for (UINT32 i = 0; i < ndev; i++) devs[i]->Release();
    CoTaskMemFree(devs);
    if (FAILED(hr)) return false;

    // Reader-creation attributes: enable the built-in video processor so MF
    // will transparently insert a decoder for MJPG webcams (very common).
    ComPtr<IMFAttributes> ropts;
    MFCreateAttributes(&ropts, 1);
    ropts->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), ropts.Get(), &reader)))
        return false;

    // Enumerate the formats this camera actually exposes. Very helpful for
    // debugging — integrated Windows webcams often default to MJPG these days.
    {
        DWORD idx = 0;
        fprintf(stderr, "[camera] available formats:\n");
        for (;;) {
            ComPtr<IMFMediaType> mt;
            HRESULT h = reader->GetNativeMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, &mt);
            if (FAILED(h)) break;
            GUID sub; mt->GetGUID(MF_MT_SUBTYPE, &sub);
            UINT32 ww = 0, hh = 0;
            MFGetAttributeSize(mt.Get(), MF_MT_FRAME_SIZE, &ww, &hh);
            // The first 4 bytes of the subtype GUID are the fourcc.
            char fcc[5] = {0};
            memcpy(fcc, &sub.Data1, 4);
            for (int i = 0; i < 4; i++) if (fcc[i] < 32 || fcc[i] > 126) fcc[i] = '?';
            fprintf(stderr, "    [%lu] %s %ux%u\n", idx, fcc, ww, hh);
            idx++;
            if (idx > 40) break; // sanity
        }
    }

    // Try to negotiate NV12 → YUY2 → MJPG (decoded by MF) → RGB24.
    auto try_set = [&](const GUID& subtype, uint32_t fourcc) -> bool {
        ComPtr<IMFMediaType> mt;
        if (FAILED(MFCreateMediaType(&mt))) return false;
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        mt->SetGUID(MF_MT_SUBTYPE,    subtype);
        MFSetAttributeSize(mt.Get(), MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
        if (FAILED(reader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt.Get())))
            return false;

        // Read back what we actually got.
        ComPtr<IMFMediaType> actual;
        if (FAILED(reader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual))) return false;
        UINT32 aw = 0, ah = 0;
        MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &aw, &ah);
        w_ = (int)aw; h_ = (int)ah;
        pixfmt_ = fourcc;
        return true;
    };

    // Enable the MF pipeline's built-in MJPG→NV12 decoder. Must do this
    // BEFORE SetCurrentMediaType, via the attribute we passed at reader
    // creation — so we set it on an attribute store and recreate the reader.
    // Simpler: set MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING on the reader
    // creation attributes. But we already created the reader. So instead we
    // ask MF to convert on-the-fly by requesting NV12 even if native is MJPG;
    // the reader will transparently insert a decoder.
    bool ok = try_set(MFVideoFormat_NV12, FOURCC_NV12)
           || try_set(MFVideoFormat_YUY2, FOURCC_YUY2)
           || try_set(MFVideoFormat_RGB24, FOURCC_RGB24);
    if (!ok) {
        fprintf(stderr, "[camera] couldn't negotiate NV12, YUY2, or RGB24\n");
        fprintf(stderr, "[camera] see available formats above — if MJPG is\n"
                        "         listed your camera is fine but the app needs\n"
                        "         MF's video processor enabled (not a quick fix)\n");
        return false;
    }

    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    reader_ = reader.Detach();
    const char* fmt = pixfmt_ == FOURCC_NV12 ? "NV12"
                    : pixfmt_ == FOURCC_YUY2 ? "YUY2" : "RGB24";
    fprintf(stdout, "[camera] negotiated %dx%d %s\n", w_, h_, fmt);
    return true;
}

bool Camera::grab(uint8_t* rgb) {
    if (!reader_) return false;
    IMFSourceReader* reader = (IMFSourceReader*)reader_;

    DWORD flags = 0; LONGLONG ts = 0;
    ComPtr<IMFSample> sample;
    // Non-blocking read: pass MF_SOURCE_READER_CONTROLF_DRAIN? No — we want
    // the normal async-to-sync read; the ReadSample with these flags blocks
    // until a frame is ready. For non-blocking we'd use the async callback
    // interface which complicates things a lot. Good enough in practice:
    // ReadSample here returns quickly because MF buffers the most recent frame.
    HRESULT hr = reader->ReadSample(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
        nullptr, &flags, &ts, sample.GetAddressOf());
    if (FAILED(hr) || !sample) return false;
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;

    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;

    BYTE* data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    if (FAILED(buf->Lock(&data, &maxLen, &curLen))) return false;

    // Determine stride.
    int stride;
    if      (pixfmt_ == FOURCC_NV12)  stride = w_;
    else if (pixfmt_ == FOURCC_YUY2)  stride = w_ * 2;
    else /* RGB24 */                   stride = w_ * 3;

    if (pixfmt_ == FOURCC_NV12)       nv12_to_rgb(data, stride, w_, h_, rgb);
    else if (pixfmt_ == FOURCC_YUY2)  yuy2_to_rgb(data, stride, w_, h_, rgb);
    else {
        // RGB24 arrives as BGR bottom-up from MF. Flip + swap.
        for (int y = 0; y < h_; y++) {
            const uint8_t* sr = data + (h_ - 1 - y) * stride;
            uint8_t* dst = rgb + y * w_ * 3;
            for (int x = 0; x < w_; x++) {
                dst[0] = sr[2]; dst[1] = sr[1]; dst[2] = sr[0];
                sr += 3; dst += 3;
            }
        }
    }

    buf->Unlock();
    return true;
}
