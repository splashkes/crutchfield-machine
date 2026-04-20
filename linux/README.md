# Linux build

A transform-based Linux port of the feedback machine. The core source
tree at the repo root stays Windows-first; this subdirectory holds a
small set of scripts and platform-specific glue that produce a native
Linux build without forking the core. See
[ADR-0014](../development/ADR/0014-platform-transforms-for-mac-and-linux.md)
for the design rationale.

## Layout

```
linux/
├── Makefile                    orchestrates prepare → compile → link
├── camera_v4l2.cpp             V4L2 capture, state behind opaque impl_
├── scripts/
│   ├── prepare_sources.py      patches main.cpp, camera.h, overlay.cpp
│   └── prepare_resources.py    copies shaders + presets + js + music,
│                               rewrites GLSL #version 460 → 450
└── README.md                   you are here
```

No `main.cpp`, `camera.cpp`, or shader copies live in here — the Windows
tree at the repo root is the single source of truth.

## What the transforms do

`scripts/prepare_sources.py` applies a handful of targeted string patches
to produce `build/generated/`:

- `main.cpp`
  - `run_mode_picker()` call gated in `#ifdef _WIN32` (picker is
    a double-click-the-exe affordance; no equivalent on Linux)
  - Help text: `Usage: feedback.exe` → `Usage: feedback`
  - Help text: picker note prefixed "On Windows only:"
  - ffmpeg encoder probe: `NUL >nul 2>&1` → `/dev/null >/dev/null 2>&1`
  - GL context hint `GLFW_CONTEXT_VERSION_MINOR` 6 → 5

- `camera.h` — private block now branches on `_WIN32`:
  - Windows keeps its Media Foundation `reader_` pointer
  - Other platforms get an opaque `void* impl_` for their native backend

- `overlay.cpp` — embedded shader strings `#version 460 core` → `#version 450 core`

Every patch goes through `replace_once()`, which fails loudly if the
expected source snippet drifts. If the Windows source changes shape,
the Linux build will refuse to prepare rather than silently miscompile.

`scripts/prepare_resources.py` assembles `build/resources/`:

- copies `shaders/`, `presets/`, `js/`, `music/` from the repo root
- rewrites GLSL `#version 460 core` → `#version 450 core` in
  `main.vert`, `main.frag`, `blit.frag`

## Platform-specific code

- `camera_v4l2.cpp` implements `Camera::open`/`grab`/dtor against
  V4L2 with YUYV→RGB software conversion. All state (fd, mmap buffers,
  pixel format) lives in a `V4L2Impl` struct stored behind the
  transformed `camera.h`'s opaque `impl_` pointer — mirroring how
  `macOS/camera_avfoundation.mm` organises AVFoundation state.
- `recorder.cpp`, `input.cpp`, `audio.cpp`, `music.cpp` compile
  unchanged from the repo root. They already handle non-Windows via
  existing `#ifdef` branches.
- QuickJS (C99) is compiled with `_GNU_SOURCE` defined so glibc exposes
  `localtime_r` and `struct tm::tm_gmtoff`.

## Dependencies (Ubuntu 22.04+ / Debian)

```
sudo apt install build-essential python3 pkg-config \
                 libglfw3-dev libglew-dev libgl1-mesa-dev \
                 libasound2-dev zlib1g-dev ffmpeg
```

- `build-essential` — `g++`, `gcc`, `make`
- `libglfw3-dev`, `libglew-dev`, `libgl1-mesa-dev` — window + GL loader + GL headers
- `libasound2-dev` — ALSA headers (miniaudio's default Linux backend)
- `zlib1g-dev` — transitive dep of the EXR writer
- `ffmpeg` — runtime only, for the post-session MP4 encode prompt
- V4L2 headers ship with the kernel headers and are already present via
  the standard toolchain

## Build

```
cd linux
make
```

Outputs:

```
build/bin/feedback               # the binary
build/resources/shaders/         # runtime-loaded shaders (GLSL 4.50)
build/resources/presets/         # feedback presets
build/resources/js/              # QuickJS engine
build/resources/music/           # .strudel music presets
```

## Run

Launch from the resource tree so the cwd-relative asset paths resolve:

```
cd build/resources
../bin/feedback
```

Or produce a portable dist and run from there:

```
make dist                        # feedback-linux-x64.tar.gz
tar -xzf feedback-linux-x64.tar.gz
cd feedback-linux-x64
./feedback
```

The dist tree bundles the binary, shaders, presets, js, music,
LICENSE, CREDITS, and this README — fully self-contained apart from
the system libraries in the Dependencies list above.

## Camera

V4L2 scans `/dev/video0`…`/dev/video9` for a device that can stream
YUYV at the requested resolution. If nothing is found, the app prints
a one-line notice and continues with the external-input layer disabled
— the rest of the instrument runs normally.

MJPEG and raw-RGB capture paths are intentionally not implemented:
every USB webcam shipped in the last fifteen years supports YUYV, and
avoiding alternative paths keeps the build free of `libjpeg` and
friends.

## MIDI

The Windows build registers a virtual MIDI port via `teVirtualMIDI` so
Strudel et al. see it in their output list. That driver is
Windows-only; the Linux build compiles out the MIDI implementation via
the existing `#ifdef _WIN32` stub in `input.cpp`. Hooking up a native
Linux MIDI backend (ALSA raw-midi or JACK-MIDI) is a future item.

## WSL2 notes (for development, not production use)

A fresh Ubuntu 24.04 WSL distro is a convenient build sandbox —
`make` succeeds end-to-end. Three caveats that apply to WSL only:

1. **No webcam.** `/dev/video*` is not exposed. The app falls back to
   "no external input" gracefully.
2. **OpenGL version.** WSLg's Mesa llvmpipe exposes OpenGL 4.5 core
   — exactly what this port targets, so a smoke test works.
3. **No audio device.** ALSA prints several `cannot find card '0'`
   warnings on startup. Harmless; miniaudio falls back to a
   null-device.

For the project's actual purpose — real-time video feedback — use a
machine with a real GPU. WSL2 is only useful here for validating that
the build compiles and links.
