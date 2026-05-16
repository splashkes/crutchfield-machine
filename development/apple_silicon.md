# Apple Silicon Notes

This file is the canonical note sheet for the native macOS / Apple
Silicon path. Keep platform-specific caveats here instead of scattering
them through the general docs.

## Current status

- The root app builds natively on Apple Silicon via either
  `Makefile.macos` (in-tree) or `macOS/Makefile` (packages an
  `.app` bundle).
- Rendering uses GLFW + GLEW on Apple's OpenGL 4.1 core profile.
- The camera path is native AVFoundation (`camera_avfoundation.mm`).
- MIDI in/out via CoreMIDI (`macOS/midi_coremidi.mm`); DDJ-FLX2 has
  a built-in default map with LED feedback for performance layers
  and VFX bank.
- Audio output + music engine (miniaudio + QuickJS pattern engine)
  compile and run on macOS — same feature set as the Windows
  reference build.
- The macOS port compiles the root `main.cpp` directly; there is no
  forked source. All platform diffs are inline `#ifdef __APPLE__`
  guards in shared files. See ADR-0014's 2026-05-16 update.

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

### Keyboard defaults

Apple builds carry an additive macOS keyboard layer on top of the
cross-platform defaults. The PC-oriented bindings still exist, but the
important missing-key actions also get Command-based aliases:

- `Cmd+Enter` fullscreen
- `Cmd+\` screenshot
- `Cmd+S` / `Cmd+N` / `Cmd+P` presets
- `Cmd+Opt+P` physics, `Cmd+Opt+T` thermal
- `Cmd+Opt+B/C/N/F` for blur / CA / noise / fields quality
- `Cmd+Opt+1..0` thermal parameter nudges

Legacy `bindings.ini` files created before these aliases existed do not
need to be deleted. On macOS, startup backfills the missing Command-key
aliases after the file is loaded.

## Known rough edges

- `Makefile.macos` (root) produces a bare executable for in-tree
  testing. `macOS/Makefile` builds a proper `feedback.app` bundle
  with Homebrew dylibs relocated into `Contents/Frameworks/` and
  ad-hoc codesigned — that's the distribution path.
- No notarization yet; Gatekeeper will require manual approval on
  fresh Macs without right-click → Open.
- Camera permission UX is per-app-bundle, so `feedback.app` gets a
  clean prompt; the bare `Makefile.macos` binary is more brittle.

## When to update this file

Update this file when any of the following changes:

- macOS build dependencies
- the `Makefile.macos` workflow
- packaging artifact names
- camera backend behavior
- permission / app-bundle guidance
- known platform-specific caveats
