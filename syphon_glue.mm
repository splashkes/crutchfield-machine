// syphon_glue.mm — implementation of the Syphon publisher.
//
// Objective-C++ because Syphon's SyphonOpenGLServer is an Objective-C
// class. The .mm extension lets us include both <Syphon/Syphon.h> and
// be invoked from plain C++ via the syphon_glue.h extern "C" interface.

#import "syphon_glue.h"

#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#import <Syphon/Syphon.h>

namespace {
SyphonOpenGLServer* g_server = nil;
NSString*           g_name   = nil;
}

extern "C" int syphon_init(const char* server_name) {
    @autoreleasepool {
        NSString* name = server_name ? [NSString stringWithUTF8String:server_name]
                                     : @"Crutchfield Machine";
        if (g_server && [g_name isEqualToString:name]) return 1;
        if (g_server) {
            [g_server stop];
            g_server = nil;
        }
        CGLContextObj cgl = CGLGetCurrentContext();
        if (!cgl) {
            fprintf(stderr, "[syphon] no current GL context — init skipped\n");
            return 0;
        }
        g_server = [[SyphonOpenGLServer alloc] initWithName:name
                                                    context:cgl
                                                    options:nil];
        if (!g_server) {
            fprintf(stderr, "[syphon] init failed\n");
            return 0;
        }
        g_name = name;
        fprintf(stdout, "[syphon] publishing as '%s'\n",
                [name UTF8String]);
        return 1;
    }
}

extern "C" void syphon_shutdown(void) {
    @autoreleasepool {
        if (g_server) {
            [g_server stop];
            g_server = nil;
            g_name = nil;
        }
    }
}

extern "C" void syphon_publish(uint32_t tex_target, uint32_t tex_id,
                               int width, int height) {
    if (!g_server || width <= 0 || height <= 0 || tex_id == 0) return;
    @autoreleasepool {
        NSRect region = NSMakeRect(0, 0, width, height);
        NSSize size   = NSMakeSize(width, height);
        [g_server publishFrameTexture:tex_id
                        textureTarget:tex_target
                          imageRegion:region
                    textureDimensions:size
                              flipped:NO];
    }
}

extern "C" int syphon_running(void) { return g_server != nil ? 1 : 0; }
