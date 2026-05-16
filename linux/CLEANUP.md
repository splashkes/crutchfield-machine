# Linux port cleanup notes

The linux port (this directory) is in a **mostly OK** state after PR #13
landed: `scripts/prepare_sources.py` was trimmed to its one remaining
real transform (GL context minor: 4.6 → 4.5) and `prepare_resources.py`
still copies shaders/presets/js/music verbatim. The build should work
unchanged.

This doc captures the deeper cleanup that would bring linux in line with
macOS — direct-compile of root sources, no Python prep step.

## Why bother

macOS used to maintain a 4291-line `macOS/main.cpp` fork plus 348 lines
of prep scripts. PR #13 deleted all of it; the macOS Makefile now compiles
root `main.cpp` directly with platform differences sitting behind `#ifdef`
in shared source. Net: −4659 / +162 lines, plus macOS gained features
(music engine, audio output) that the fork was missing.

The same simplification is available for linux. The script is now
nearly empty — one real transform — so the question is whether to keep
the script-based path for consistency or follow macOS's lead.

## What's left in the linux prep path

1. **`prepare_sources.py`** does one transform now:
   - `GLFW_CONTEXT_VERSION_MINOR, 6` → `5` (the linux GL hint).
   - Copies `camera.h` and `overlay.cpp` unchanged (the historical
     transforms became obsolete when root made those files cross-platform).
2. **`prepare_resources.py`** does no transforms — pure file copies.
   The `#version 460 → 450` replace is a no-op since root shaders are
   already at `#version 410`.

## Path to delete the script entirely

Three small changes in root, then `linux/scripts/` can go away:

1. **GL context hint:** replace the 2-way `#ifdef __APPLE__` block in
   `configure_gl_context_hints()` (root `main.cpp`) with a 3-way:

   ```cpp
   #ifdef __APPLE__
       glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
       glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
   #elif defined(__linux__)
       glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
   #else
       glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
   #endif
   ```

   (Linux 4.5 is conservative — most modern Mesa/NVIDIA drivers support
   4.6 fine, so an alternative is to just bump linux to 4.6 and skip the
   extra branch. Test on the target box first.)

2. **`linux/Makefile`:** replace the `$(GEN_DIR)/main.cpp` recipe with a
   direct compile of `$(ROOT)/main.cpp`. Drop `$(GEN_DIR)` and the
   `prepare_sources` target entirely. The macOS Makefile (`macOS/Makefile`)
   is the working reference for this pattern.

3. **`linux/Makefile`:** replace the `prepare_resources` invocation with
   inline `cp -r` commands (again mirroring macOS).

4. **Delete `linux/scripts/`** once the Makefile changes are committed.

## What stays

- `linux/Makefile` (with the simplifications above)
- `linux/camera_v4l2.cpp` — the v4l2 backend that implements the
  `Camera::impl_` pimpl that root's cross-platform `camera.h` declares.
- `linux/README.md` — needs a one-paragraph update mentioning the
  direct-compile architecture (mirror what `macOS/README.md` says).

## Testing

I had no Linux host available during the audit, so the trimmed
`prepare_sources.py` is the only verified change. The deeper cleanup
above is mechanical but should be done on a machine that can actually
run `make` in `linux/` to catch any platform-specific surprises (the
v4l2 backend, ALSA linkage, etc.).

## Pre-existing observations

- `linux/Makefile` already builds QuickJS, miniaudio (via `audio.cpp`),
  and the full music engine — so unlike the old macOS fork, linux has
  not been drifting feature-wise. The cleanup is purely about the
  build-architecture moving part, not about feature parity.
- `linux/README.md` is currently accurate. Update only after the
  Makefile is simplified.
