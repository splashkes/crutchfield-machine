# ADR-0014 — Platform ports live as build-time transforms, not forks

**Status:** Superseded for macOS (2026-05-16) — see "Update" below.
   Linux retains a one-transform script; see `linux/CLEANUP.md` for
   the path to align it with the macOS approach.
**Date:** 2026-04-20
**Retroactive:** no

## Update — 2026-05-16

The prep-script approach was abandoned for the macOS port. Root
`main.cpp` had grown enough inline `#ifdef __APPLE__` / `#ifdef _WIN32`
guards that the script's transforms became mostly obsolete, and
`macOS/main.cpp` had silently drifted into a 4291-line hand-edited
fork (missing music + audio entirely). PR #13 deleted the fork and
the macOS prep scripts; `macOS/Makefile` now compiles root sources
directly, with `bootstrap_macos_runtime()` and the small set of mac
adaptations sitting behind `#ifdef __APPLE__` in shared source.

The linux port retains its prep script but it has been trimmed to a
single transform (GL context-minor 4.6 → 4.5). The path to full
unification is documented in `linux/CLEANUP.md`.

The original decision below is preserved for context — it correctly
diagnosed the drift problem; in practice inline `#ifdef` guards
turned out to be cheaper than maintaining the transform list,
because the script's "fail loudly on drift" property cuts both ways:
it also failed loudly every time root added a feature that didn't
need a platform transform at all.

---

## Context

The instrument is Windows-first: `main.cpp`, `camera.h/cpp`, and the
rest of the core live at the repo root and are developed against
Windows (Media Foundation for camera, GLEW loader, GL 4.6). Two other
platforms need a working build:

- **macOS** (Apple Silicon) — no Media Foundation, OpenGL capped at
  4.1, AVFoundation for camera, `.app` bundle + codesigning + Homebrew
  dylib rewrites for distribution.
- **Linux** (Ubuntu/Debian) — V4L2 for camera, Mesa's GL ceiling of
  4.5 under llvmpipe and in many Wayland sessions, `libasound2-dev`
  for miniaudio's default backend.

A first Linux attempt lived in `linux/` as a hand-maintained fork of
`main.cpp`, `camera.cpp`, and the shader tree. That fork drifted the
moment the Windows tree added features (music mode, QuickJS, new
shader layers) and became dead weight — the POC compiled but didn't
match the current instrument.

The question was how to keep each platform building without paying
that drift cost.

## Decision

Each platform port is a subdirectory (`macOS/`, `linux/`) containing:

1. A **Python prep script** (`prepare_sources.py`) that reads the
   canonical Windows source files from the repo root and writes
   platform-adapted copies into `<platform>/build/generated/`. It
   applies only the surgical patches that platform needs. Every
   patch goes through a `replace_once()` helper that throws if the
   expected snippet isn't found verbatim — so drift in the Windows
   source fails the port build loudly rather than silently
   miscompiling.
2. A **resource prep script** (`prepare_resources.py`) that copies
   `shaders/`, `presets/`, `js/`, `music/` into
   `<platform>/build/resources/` and applies any shader-level
   transforms (e.g. GLSL `#version` downgrades).
3. A **platform-specific camera implementation**
   (`macOS/camera_avfoundation.mm`, `linux/camera_v4l2.cpp`) that
   mirrors the `Camera` public API and stores all backend state
   behind an opaque `void* impl_` pointer in the transformed
   `camera.h`.
4. A **platform Makefile** that orchestrates prep → compile → link,
   also handling platform-specific packaging (`.app` bundling on
   mac, `.tar.gz` dist on Linux).

The only files that live per-platform in source control are: the
scripts, the platform camera backend, the Makefile, and the README.
No `main.cpp` fork, no shader fork, no duplicated `recorder.cpp` or
`overlay.cpp`.

The canonical Windows tree is edited freely. When a core edit touches
a snippet that a platform transform expected, the platform build
breaks at prep time with a clear error naming the missing snippet —
the fix is to update the patch, not the port.

## Consequences

**Wins**

- Single source of truth for everything the user actually cares about:
  shaders, feedback logic, recorder, overlay, input.
- Adding a new Windows feature doesn't require thinking about mac or
  Linux ports at commit time. Either the transforms still match (done)
  or prep breaks with a named-snippet error (a one-line fix in the
  patch set).
- Each platform's adaptations are visible in one file per port
  (`prepare_sources.py`), readable in a few minutes. Reviewers can
  audit exactly what differs.
- The platform Makefiles each produce a proper native artifact —
  `.app` for mac, `tar.gz` for Linux — without touching the Windows
  Makefile.

**Costs**

- Patches are string matches. Formatting changes to `main.cpp`
  (whitespace, comment rewording) that aren't substantive will still
  break prep. Mitigation: rerun prep after any nontrivial core edit
  during development; the error message names the failing snippet.
- Patches are not expressive. Anything fancier than "replace this
  block with that block" would need a proper AST pass. So far every
  adaptation has been small and local; if that changes (e.g. big
  platform-specific logic needs to slot into the middle of a
  function), we'd need to introduce seam points in the Windows source
  rather than grow the patches.
- Reading the Linux build end-to-end requires reading
  `scripts/prepare_sources.py` alongside the Windows source — the
  generated file in `build/generated/main.cpp` is the actual compile
  unit. Not discoverable to someone grepping `main.cpp` alone.

## Alternatives considered

- **Ifdef everything in the core.** Put `#ifdef __APPLE__` /
  `#ifdef __linux__` branches directly in `main.cpp`. Rejected: the
  Windows file is already ~3000 lines and growing with music/MIDI
  features. Adding two more platform branch sets per changed region
  would bury the instrument logic in platform plumbing.
- **Per-platform `main_<os>.cpp` forks.** What the original
  `linux/main.cpp` did. Rejected because it already drifted; there's
  no reason to repeat that mistake on mac.
- **A single cross-platform rewrite.** Vulkan/MoltenVK + portable
  camera abstraction + portable audio. Deferred — a multi-week rewrite
  versus a one-day transform setup. ADR to revisit when the Windows
  tree stabilizes enough that cross-platform refactoring is worth the
  disruption.
- **CMake + find_package.** Would replace the Makefiles but not the
  core question of how to handle platform-specific source divergence.
  Orthogonal. The platform Makefiles are ~60 lines each and don't
  warrant the CMake migration today.

## References

- `macOS/scripts/prepare_sources.py`, `macOS/scripts/prepare_resources.py`
- `macOS/camera_avfoundation.mm`
- `linux/scripts/prepare_sources.py`, `linux/scripts/prepare_resources.py`
- `linux/camera_v4l2.cpp`
- `macOS/Makefile`, `linux/Makefile`
- Root `README.md` "Platform ports" section
- `development/RUNBOOK.md` "Platform ports" section
