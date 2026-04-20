# ADR-0010 — Keep the root app on Apple Silicon via OpenGL 4.1 + AVFoundation

**Status:** Accepted
**Date:** 2026-04-20
**Retroactive:** no

## Context

The project historically treated macOS as a notes-only side path. The
root application was effectively Windows-first, with `camera.cpp`
implemented in Media Foundation and the root build targeting OpenGL 4.6.

That left Apple Silicon users with no practical path to run the actual
instrument on current `main`, even though most of the renderer, overlay,
recorder, and state/input system were already cross-platform C++.

The main constraints were:

- Apple ships OpenGL 4.1 core, not 4.6.
- Camera capture on macOS needs AVFoundation, not Media Foundation.
- Rewriting the root app as Metal was far larger than needed for the
  current shader set.

## Decision

The root application supports Apple Silicon directly.

We do this by:

- keeping the root renderer on OpenGL, but requesting a macOS-compatible
  4.1 core context on Apple platforms
- compiling the root shaders and overlay shaders as GLSL 4.10-compatible
  source
- adding a native AVFoundation camera backend in `camera_avfoundation.mm`
  behind the existing `Camera` API
- adding `Makefile.macos` as the canonical native Apple Silicon build and
  packaging path

We do **not** fork a separate macOS host application or do a Metal port
for this phase.

## Consequences

**Positive:**

- Apple Silicon users can run the current root app, including the latest
  input / V-4 work, without waiting for a separate port.
- Most of the codebase stays shared: main loop, recorder, overlay,
  shaders, presets, and input system all remain in one host path.
- The camera path stays encapsulated behind `Camera::open/grab`, so the
  main loop remains platform-agnostic.

**Negative:**

- The root shader surface is now constrained by Apple's OpenGL 4.1
  ceiling. Future shader features must respect that or grow conditional
  compilation.
- Camera behavior now has two platform backends that must remain roughly
  in sync.
- macOS packaging is still immature compared with Windows distribution:
  Homebrew runtime deps, bare-binary permissions, no `.app`/codesign yet.

## Alternatives considered

- **Leave macOS as notes-only.** Cheapest short-term, but keeps a working
  Apple Silicon build permanently out of reach.
- **Separate macOS host under `macOS/` with its own main loop.** Avoids
  touching the root app, but duplicates host logic and drifts faster.
- **Full Metal port.** Long-term attractive, but disproportionate for the
  immediate goal of getting the existing instrument running on Apple
  Silicon.

## References

- `main.cpp::configure_gl_context_hints`
- `camera.h`, `camera.cpp`, `camera_avfoundation.mm`
- `Makefile.macos`
- `development/apple_silicon.md`
