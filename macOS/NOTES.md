# Mac port — sync notes vs. current Windows version

## Status: SKELETON ONLY — not a working program

This tree contains **only the partial shader layer files** that were
translated to Metal Shading Language during early experimentation. There
is **no host code** (no Swift, no Objective-C, no Makefile), and several
shader layers are also missing. This cannot be built or run as-is.

This zip is included as a starting point for a hypothetical future Mac
port, not as a functional program. If you want to use the instrument on
macOS, the realistic option today is to build the Linux tree on your
machine via Homebrew (since GLFW + GLEW work fine on Apple Silicon with
the deprecated macOS OpenGL 4.1 support) — though that path caps you at
OpenGL 4.1, which is enough for the current shader set but limits
future extensions.

**The "proper" Mac port with Metal is multi-day work, not file-copying.**

---

## What IS in this tree

### Shader layers (5 of 12)
- `shaders/common.metal` — utility functions (RGB↔HSV, hash21, etc.)
- `shaders/layers/warp.metal`
- `shaders/layers/optics.metal`
- `shaders/layers/gamma.metal`
- `shaders/layers/color.metal`
- `shaders/layers/contrast.metal`

These are direct transliterations from the corresponding GLSL files.
They were written but never compiled.

---

## What is MISSING

### Shader layers (7 of 12 missing)
- `decay.metal`
- `noise.metal`
- `couple.metal`
- `external.metal`
- `inject.metal`
- `physics.metal` — Crutchfield-faithful camera-side physics
- `thermal.metal` — turbulent air displacement

These need to be translated from their GLSL counterparts in the Windows
tree. The translations are largely mechanical (GLSL and MSL are both
C-like shader languages with similar vector math), but each has
subtleties:
- Sampler types: MSL uses `sampler` and `texture2d<float>` instead of
  `sampler2D`.
- Texture sampling: MSL is `tex.sample(samp, uv)`, not `texture(tex, uv)`.
- Built-ins: GLSL `mod()` → MSL `fmod()` or `metal::fmod`.
- `gl_FragCoord` → `[[position]]` stage input.
- Constants: `#define` works but prefer `constant` globals.

Rough estimate: 1–2 hours to port the 7 remaining shaders.

### Orchestrator shader (missing)
`main.metal` — the Metal equivalent of `main.frag`. Needs:
- `Uniforms` struct matching the host-side layout (must be careful about
  Metal's 16-byte alignment rules — different from GLSL/std140).
- Vertex shader `vs_main` producing a fullscreen triangle.
- Fragment shader `fs_feedback` orchestrating all 12 layer calls.
- Fragment shader `fs_blit` for the final display pass.

### Host code (all missing — this is the largest gap)
The Windows version has ~1700 lines of C++ host code. Everything below
would need to be written from scratch for Mac:

1. **Window and Metal setup**
   - `NSApplication` / `NSWindow` bootstrap
   - `MTKView` or custom `CAMetalLayer` integration
   - `MTLDevice`, `MTLCommandQueue`, `MTLRenderPipelineState`
   - Shader concatenation: read all `.metal` files from disk, assemble
     into one source string, compile as `MTLLibrary`
   - Ping-pong texture pairs with private storage mode

2. **Main render loop**
   - Per-frame `MTLCommandBuffer` with two render passes (feedback +
     blit).
   - Uniform struct upload via `setFragmentBytes`.
   - Multi-field support (up to 4 coupled fields, same ring topology as
     Windows).

3. **Camera input**
   - `AVCaptureSession` with `AVCaptureDevice` for the default camera.
   - `CVMetalTextureCache` to get zero-copy `MTLTexture` from
     `CMSampleBuffer`.
   - Permission prompt handling on first run (Info.plist key
     `NSCameraUsageDescription` required).

4. **Keyboard input**
   - `NSEvent` key handler on a custom `MTKView` subclass.
   - All the keybindings from Windows (~60 keys, including F1–F12
     modifier combos, Shift-for-coarse, Ctrl+S/N/P for presets).

5. **Overlay/HUD**
   - Option A: port `stb_easy_font` text rendering to Metal (it's just
     quads with positions, any API can draw it).
   - Option B: use Core Text / `CATextLayer` for higher-quality text,
     but that's a bigger integration.

6. **Preset system** — save/load/cycle `.ini` files. Pure C++ code
   in the Windows build, can be imported as-is or rewritten in Swift.

7. **Recording subsystem (the largest piece)**
   - Metal doesn't have OpenGL's `glReadPixels` + PBO pattern directly.
     The Metal equivalent is `MTLBlitCommandEncoder::copy(from:to:)` into
     a shared-storage-mode `MTLBuffer`, then reading the buffer on
     CPU after the command buffer completes.
   - Fence-async pattern: `MTLSharedEvent` or the completion handler of
     the command buffer (`addCompletedHandler`).
   - Background thread with its own `MTLCommandQueue` (queues are
     thread-safe; you just need your own one per thread for sanity).
   - The EXR writer (`exr_write.h`) ports unchanged — it's pure C.
   - Post-exit encode prompt: `Process` or `NSTask` for ffmpeg
     subprocess.

   Porting complexity estimate: comparable to writing the Windows
   recorder from scratch (~600 LOC), but in a different API family.

8. **CLI argument parsing** — trivially portable C++ code.

### Dependencies
- Xcode command-line tools (`swiftc`, `clang++`).
- No third-party libraries are strictly required for a Mac build —
  everything's in the macOS SDK.

---

## Recommended approach if you want a real Mac port

**Phase 1 — Ship a basic version (1–2 days)**
1. Port the 7 missing shaders to MSL.
2. Write `main.metal` orchestrator.
3. Write minimal Swift host: `main.swift`, `camera.swift`. No overlay,
   no presets, no recording. Just the live feedback experience with
   keyboard controls.
4. Verify shader math matches the Windows output pixel-for-pixel on a
   test pattern.

At this point you'd have a Mac instrument roughly equivalent to the
very first iteration of the project.

**Phase 2 — Feature parity (2–3 days)**
5. Overlay/HUD with `stb_easy_font`.
6. Preset system (port directly from Windows C++).
7. Fine step sizes, all CLI flags, hot-reload shaders.

**Phase 3 — Recording (1–2 days)**
8. EXR-sequence recorder using `MTLBlitCommandEncoder` + shared-storage
   `MTLBuffer` + completion handler.
9. Post-exit encode prompt.

**Total: roughly 4–7 days of focused work** to reach parity with the
current Windows build. This is meaningfully more than the Linux port
(which is maybe 3–4 days) because the Metal API is different enough from
OpenGL that you're rewriting host code rather than adjusting it.

---

## Alternative: MoltenVK

If the goal is "run the instrument on a Mac without rewriting" rather
than "native Metal," another option is to port Linux to MoltenVK — a
Vulkan-on-Metal translation layer that lets you use a single Vulkan
codebase across Windows/Linux/Mac. The current code is OpenGL though, not
Vulkan, so this would be a full Vulkan port of the Linux tree, which is
comparable effort to a native Metal port but results in a cross-platform
Vulkan codebase.

Verdict: if you want a Mac instrument specifically, write Metal. If you
want cross-platform sanity for the next decade, invest in a Vulkan port
of the whole project.

---

## Files to reference when porting

All in the current Windows tree:
- `main.cpp` — host code reference
- `shaders/main.frag` — orchestrator reference
- `shaders/layers/*.glsl` — the 7 missing shaders to port
- `recorder.cpp` / `exr_write.h` — EXR recorder design reference
- `overlay.cpp` — HUD reference

Philosophy and academic credits are in `PHILOSOPHY.md` and `CREDITS.md`
in this zip. Both apply to all platforms equally — the Mac port (if it
happens) is the same instrument, just running on different metal.
