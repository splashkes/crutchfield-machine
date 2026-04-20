# macOS port notes

This subdirectory holds notes and shaders toward a macOS port. There is
**no runnable app in this subdirectory yet** — just documents and the shared
shader tree.

If you received a separate `feedback-macos-arm64` bundle outside this repo,
that artifact may run today, but it is currently not self-contained on a
fresh Apple Silicon machine. The shipped `feedback` binary links against
Homebrew's `glfw` and `glew` dylibs, so first launch requires:

```bash
brew install glfw glew
```

If `dyld` reports `Library not loaded` for
`/opt/homebrew/opt/glfw/lib/libglfw.3.dylib` or
`/opt/homebrew/opt/glew/lib/libGLEW.2.3.dylib`, the machine is missing those
runtime dependencies. `pkg-config` is only needed for source builds, not for
running a prebuilt binary.

The current prebuilt macOS binary also resolves `shaders/`, `presets/`, and
`bindings.ini` from the shell's current working directory rather than from the
binary's own folder. Launch it from inside the extracted directory:

```bash
cd /path/to/feedback-macos-arm64
./feedback
```

If you run it by absolute path from some other directory, startup may fail
with `can't open shaders/main.vert`.

## What's here

- `shaders/` — same shader source as the Windows build.
- `NOTES.md`, `PHILOSOPHY.md`, `CREDITS.md` — context material.

## What's missing

Everything else. No `main.cpp`, no build system, no camera capture code.

## Planned approach

- GL 4.1 core profile (the highest Apple ships; no compute shaders).
- AVFoundation for camera capture instead of Media Foundation / V4L2.
- GLFW for window + input — same as Windows.
- Recorder pipeline ported with minor changes (same EXR code should work;
  PBO behavior identical on desktop GL).

## Status

This tracked repo subtree is still **not started** as a complete macOS source
port. If you want to help, see `../CONTRIBUTING.md`. The Windows build at the
repo root is the reference.
