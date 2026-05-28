// syphon_glue.h — minimal C interface around SyphonOpenGLServer.
//
// Publishes Crutchfield's rendered feedback texture as a Syphon source
// that any Syphon-aware app on the same Mac can subscribe to (Resolume,
// MadMapper, VDMX, TouchDesigner, OBS via the Syphon plugin, etc.).
//
// macOS-only. On other platforms the header defines empty inline stubs
// so call sites in main.cpp don't need #ifdefs everywhere — they can
// just call syphon_publish() and it's a no-op off-mac.

#pragma once

#include <stdint.h>

// Stub the entire interface when:
//   - we're not on macOS (no Syphon there)
//   - OR the build opted out (-DCRUTCHFIELD_NO_SYPHON), typically because
//     vendor/syphon isn't available at link time
#if !defined(__APPLE__) || defined(CRUTCHFIELD_NO_SYPHON)
static inline int  syphon_init(const char*) { return 0; }
static inline void syphon_shutdown(void)    {}
static inline void syphon_publish(uint32_t, uint32_t, int, int) {}
static inline int  syphon_running(void)     { return 0; }
#else

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

#endif  // !__APPLE__ || CRUTCHFIELD_NO_SYPHON
