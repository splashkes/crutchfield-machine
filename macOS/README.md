# macOS port — very early work in progress

This subdirectory holds notes and shaders toward a macOS port. There is
**no runnable app here yet** — just documents and the shared shader tree.

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

Port is **not started**. If you want to help, see `../CONTRIBUTING.md`.
The Windows build at the repo root is the reference.
