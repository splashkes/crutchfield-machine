// camera_avfoundation.mm — AVFoundation webcam capture on macOS.
//
// Captures BGRA frames on a dedicated queue, converts them to top-down RGB,
// and exposes the latest frame via the existing Camera::grab polling API.

#include "camera.h"

#include <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <dispatch/dispatch.h>

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

@interface FeedbackFrameSink : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<uint8_t> latest_;
    bool hasFrame_;
    bool newFrame_;
    int width_;
    int height_;
}
- (instancetype)init;
- (bool)waitForFirstFrame:(double)timeoutSec;
- (bool)copyLatestTo:(uint8_t*)dst expectedWidth:(int)w expectedHeight:(int)h;
@end

@implementation FeedbackFrameSink

- (instancetype)init {
    self = [super init];
    if (self) {
        hasFrame_ = false;
        newFrame_ = false;
        width_ = 0;
        height_ = 0;
    }
    return self;
}

- (bool)waitForFirstFrame:(double)timeoutSec {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::duration<double>(timeoutSec), [&] {
        return hasFrame_;
    });
}

- (bool)copyLatestTo:(uint8_t*)dst expectedWidth:(int)w expectedHeight:(int)h {
    if (!dst) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!newFrame_ || width_ != w || height_ != h) return false;
    const size_t bytes = (size_t)w * h * 3;
    if (latest_.size() != bytes) return false;
    std::memcpy(dst, latest_.data(), bytes);
    newFrame_ = false;
    return true;
}

- (void)captureOutput:(AVCaptureOutput*)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)connection
{
    (void)output;
    (void)connection;

    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!image) return;

    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)image;
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    const OSType fmt = CVPixelBufferGetPixelFormatType(pixelBuffer);
    if (fmt != kCVPixelFormatType_32BGRA) {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return;
    }

    const int w = (int)CVPixelBufferGetWidth(pixelBuffer);
    const int h = (int)CVPixelBufferGetHeight(pixelBuffer);
    const size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
    const uint8_t* src = (const uint8_t*)CVPixelBufferGetBaseAddress(pixelBuffer);
    if (!src || w <= 0 || h <= 0) {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    width_ = w;
    height_ = h;
    latest_.resize((size_t)w * h * 3);

    for (int y = 0; y < h; y++) {
        const uint8_t* sr = src + (size_t)y * stride;
        uint8_t* dst = latest_.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            dst[0] = sr[2];
            dst[1] = sr[1];
            dst[2] = sr[0];
            sr += 4;
            dst += 3;
        }
    }

    hasFrame_ = true;
    newFrame_ = true;
    cv_.notify_all();

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

@end

@interface FeedbackCameraBackend : NSObject {
@public
    AVCaptureSession* session_;
    AVCaptureDeviceInput* input_;
    AVCaptureVideoDataOutput* output_;
    FeedbackFrameSink* sink_;
    dispatch_queue_t queue_;
}
@end

@implementation FeedbackCameraBackend
@end

static NSString* choosePreset(int w, int h) {
    if (w >= 1920 && h >= 1080) return AVCaptureSessionPreset1920x1080;
    if (w >= 1280 && h >= 720)  return AVCaptureSessionPreset1280x720;
    if (w >= 640  && h >= 480)  return AVCaptureSessionPreset640x480;
    return AVCaptureSessionPresetHigh;
}

static AVCaptureDevice* chooseDevice() {
    AVCaptureDeviceDiscoverySession* ds =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[
            AVCaptureDeviceTypeBuiltInWideAngleCamera,
            AVCaptureDeviceTypeExternalUnknown
        ]
        mediaType:AVMediaTypeVideo
        position:AVCaptureDevicePositionUnspecified];

    for (AVCaptureDevice* d in ds.devices) {
        if (d) return d;
    }
    return [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
}

Camera::Camera() {}

Camera::~Camera() {
    @autoreleasepool {
        FeedbackCameraBackend* impl =
            (__bridge_transfer FeedbackCameraBackend*)impl_;
        impl_ = nullptr;
        if (!impl) return;

        if (impl->output_) {
            [impl->output_ setSampleBufferDelegate:nil queue:nil];
        }
        if (impl->session_ && [impl->session_ isRunning]) {
            [impl->session_ stopRunning];
        }
    }
}

bool Camera::open(int w, int h) {
    @autoreleasepool {
        if (impl_) return true;

        AVAuthorizationStatus status =
            [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if (status == AVAuthorizationStatusDenied ||
            status == AVAuthorizationStatusRestricted) {
            std::fprintf(stderr,
                         "[camera] access denied by macOS privacy settings\n");
            return false;
        }

        if (status == AVAuthorizationStatusNotDetermined) {
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block BOOL granted = NO;
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                     completionHandler:^(BOOL ok) {
                granted = ok;
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            if (!granted) {
                std::fprintf(stderr,
                             "[camera] access request denied by user\n");
                return false;
            }
        }

        AVCaptureDevice* device = chooseDevice();
        if (!device) {
            std::fprintf(stderr, "[camera] no webcam found\n");
            return false;
        }

        NSError* err = nil;
        AVCaptureDeviceInput* input =
            [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
        if (!input) {
            std::fprintf(stderr, "[camera] input creation failed: %s\n",
                         err ? [[err localizedDescription] UTF8String] : "unknown");
            return false;
        }

        FeedbackCameraBackend* impl = [[FeedbackCameraBackend alloc] init];
        impl->session_ = [[AVCaptureSession alloc] init];
        impl->input_ = input;
        impl->output_ = [[AVCaptureVideoDataOutput alloc] init];
        impl->sink_ = [[FeedbackFrameSink alloc] init];
        impl->queue_ = dispatch_queue_create("feedback.camera.capture",
                                             DISPATCH_QUEUE_SERIAL);

        NSString* preset = choosePreset(w, h);
        if ([impl->session_ canSetSessionPreset:preset]) {
            [impl->session_ setSessionPreset:preset];
        }

        if (![impl->session_ canAddInput:impl->input_]) {
            std::fprintf(stderr, "[camera] session rejected input device\n");
            return false;
        }
        [impl->session_ addInput:impl->input_];

        impl->output_.alwaysDiscardsLateVideoFrames = YES;
        impl->output_.videoSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        [impl->output_ setSampleBufferDelegate:impl->sink_ queue:impl->queue_];

        if (![impl->session_ canAddOutput:impl->output_]) {
            std::fprintf(stderr, "[camera] session rejected video output\n");
            return false;
        }
        [impl->session_ addOutput:impl->output_];

        AVCaptureConnection* conn =
            [impl->output_ connectionWithMediaType:AVMediaTypeVideo];
        if (conn && [conn isVideoMirroringSupported]) {
            conn.videoMirrored = NO;
        }

        [impl->session_ startRunning];

        if (![impl->sink_ waitForFirstFrame:3.0]) {
            std::fprintf(stderr,
                         "[camera] timed out waiting for first frame\n");
            [impl->output_ setSampleBufferDelegate:nil queue:nil];
            [impl->session_ stopRunning];
            return false;
        }

        w_ = impl->sink_->width_;
        h_ = impl->sink_->height_;
        impl_ = (__bridge_retained void*)impl;

        std::fprintf(stdout, "[camera] negotiated %dx%d BGRA\n", w_, h_);
        return true;
    }
}

bool Camera::grab(uint8_t* rgb) {
    @autoreleasepool {
        FeedbackCameraBackend* impl = (__bridge FeedbackCameraBackend*)impl_;
        if (!impl || !impl->sink_) return false;
        return [impl->sink_ copyLatestTo:rgb expectedWidth:w_ expectedHeight:h_];
    }
}
