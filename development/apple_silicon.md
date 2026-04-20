# Apple Silicon Notes

This file is the canonical note sheet for the native macOS / Apple
Silicon path. Keep platform-specific caveats here instead of scattering
them through the general docs.

## Current status

- The root app builds natively on Apple Silicon via `Makefile.macos`.
- Rendering uses GLFW + GLEW on Apple's OpenGL 4.1 core profile.
- The camera path is native AVFoundation (`camera_avfoundation.mm`).
- The latest upstream `main` build includes the action-registry / gamepad /
  V-4 work and still builds on macOS.

## Build prerequisites

```bash
brew install glfw glew pkg-config
```

Xcode command-line tools must also be installed (`xcode-select -p` should
resolve).

## Build / run

```bash
make -f Makefile.macos
./feedback --fullscreen
```

Package a zip with:

```bash
make -f Makefile.macos dist
```

Current output name:

- `feedback-macos-arm64.zip`

## Technical notes

### OpenGL ceiling

Apple ships OpenGL 4.1 core, not 4.6. The root app handles this by:

- requesting a `4.1` core context with `GLFW_OPENGL_FORWARD_COMPAT`
- compiling the root shaders / overlay shaders as `#version 410 core`

If a future shader change needs 4.2+ features, the macOS path will break
until that is redesigned or conditionally compiled.

### Camera path

The camera backend lives in `camera_avfoundation.mm` and keeps the host
API identical to Windows:

- `Camera::open(w, h)` opens the first available camera
- `Camera::grab(rgb)` copies the latest frame into the existing RGB buffer

Internally it:

- requests macOS camera access
- starts an `AVCaptureSession`
- asks AVFoundation for BGRA frames
- converts BGRA -> RGB in the capture delegate

### Permissions

The most common failure is not a build failure but macOS privacy:

- If camera access is denied, startup logs
  `access denied by macOS privacy settings` or
  `access request denied by user`.
- Re-enable camera access in:
  `System Settings -> Privacy & Security -> Camera`

Because we currently launch a bare binary, camera permission behavior is
less polished than a signed `.app` bundle.

## Known rough edges

- The app is still a bare executable, not a packaged `.app`.
- No codesigning / notarization / bundle metadata yet.
- Camera permission UX is therefore more brittle than a normal macOS app.
- Homebrew `glfw` / `glew` are runtime dependencies; this is not yet a
  standalone redistribution story on macOS.

## When to update this file

Update this file when any of the following changes:

- macOS build dependencies
- the `Makefile.macos` workflow
- packaging artifact names
- camera backend behavior
- permission / app-bundle guidance
- known platform-specific caveats
