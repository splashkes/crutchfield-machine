# macOS / Apple Silicon

This subtree keeps the macOS build isolated from the Windows-first app at
the repo root.

## Shared-source rule

The macOS build does **not** check in near-duplicate copies of the root
Windows sources. Instead, `macOS/scripts/prepare_sources.py` transforms the
shared root `main.cpp` and `camera.h` into `build/generated/` at build time,
then compiles those generated files together with the macOS-specific camera
backend.

Shared at build time:

- `../main.cpp`
- `../camera.h`
- `../input.cpp`
- `../overlay.cpp`
- `../recorder.cpp`
- `../shaders/`
- `../presets/`

Mac-specific in this directory:

- `camera_avfoundation.mm`
- `Makefile`
- `Info.plist`
- `scripts/prepare_sources.py`

## Prereqs

```bash
brew install glfw glew pkg-config
```

## Build

```bash
cd macOS
make
```

This produces `macOS/feedback.app`.

## Package

```bash
cd macOS
make dist
```

This produces `macOS/feedback-macos-arm64.zip`.

## Launch

Double-click `feedback.app` in Finder, or:

```bash
cd macOS
open feedback.app
```

The app bundle embeds:

- `shaders/` and `presets/` in `Contents/Resources`
- `libglfw.3.dylib` and `libGLEW.2.3.dylib` in `Contents/Frameworks`

At runtime the app uses:

`~/Library/Application Support/Crutchfield Machine`

for `bindings.ini`, user presets, screenshots, and recordings, so Finder
launches do not depend on the shell working directory.

## Camera

- On first launch, macOS should ask for camera permission.
- If access was denied, re-enable it for `feedback` under
  System Settings -> Privacy & Security -> Camera.

## Note

This still needs normal signing/notarization work if you want Gatekeeper to
trust a downloaded release zip on other machines without manual override.
