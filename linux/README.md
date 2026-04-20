# Linux port — work in progress

This subdirectory contains an early Linux port of the feedback machine.
It is **not** feature-parity with the Windows build at the repo root.

## What's here

- `main.cpp`, `camera.cpp` — core app with V4L2 camera capture (not Media Foundation).
- `Makefile` — expects standard Linux dev packages (glfw, glew, v4l2).
- `shaders/` — same shader tree as the Windows build.

## What's missing (vs. the Windows build)

- `recorder.cpp` / `recorder.h` — no lossless EXR recorder yet.
- `overlay.cpp` / `overlay.h` — no HUD / help overlay.
- CLI flags parity (`--precision 8`, `--preset`, `--demo`, etc.).
- Audio reactivity, MIDI/OSC input.

## Status

Treat this as a proof-of-concept that the feedback shader stack compiles
and runs under GL on Linux. If you want to actually *use* the system, the
Windows build is the reference implementation.

Contributions welcome — see `../CONTRIBUTING.md`.
