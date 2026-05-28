// syphon_glue.h — minimal C interface around SyphonOpenGLServer.
//
// Publishes Crutchfield's rendered feedback texture as a Syphon source
// that any Syphon-aware app on the same Mac can subscribe to (Resolume,
// MadMapper, VDMX, TouchDesigner, OBS via the Syphon plugin, etc.).
//
// Requires Syphon.framework built and accessible at link time.
// See vendor/syphon/build/Release/Syphon.framework.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize a Syphon server with the given name (max ~32 chars,
// human-readable). Idempotent — calling twice with the same name is
// a no-op. Returns 1 on success, 0 if Syphon.framework wasn't loaded
// at link time or the OpenGL context isn't current.
int  syphon_init(const char* server_name);

// Shut down the server.
void syphon_shutdown(void);

// Publish the given OpenGL texture as the current Syphon frame.
//   tex_target: usually GL_TEXTURE_2D (0x0DE1)
//   tex_id:     OpenGL texture handle
//   width/height: texture dimensions in pixels
// Call once per rendered frame after the feedback is written into
// the texture but before SwapBuffers.
void syphon_publish(uint32_t tex_target, uint32_t tex_id,
                    int width, int height);

// Returns 1 if a server is currently running.
int  syphon_running(void);

#ifdef __cplusplus
}
#endif
