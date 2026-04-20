# Linux port — sync notes vs. current Windows version

## Status: OUT OF SYNC

This tree is an **early snapshot**, preserved here because it builds and
runs cleanly. The Windows version has evolved substantially past this
point. This file documents exactly what's present, what's missing, and
what it takes to bring this tree up to parity.

**Do not treat these files as equivalent to the current Windows source.**
They implement the same core idea (layered fragment-shader feedback with
ping-pong FBOs, 10 shader layers, V4L2 camera input) but lack every
feature added during the Windows evolution.

---

## What IS in this tree

### Working and verified at snapshot time
- Layered shader architecture, same orchestrator pattern as Windows
  (`main.frag` with `#include` resolution done by the host, per-layer
  `.glsl` files under `shaders/layers/`)
- 10 shader layers: `warp`, `optics`, `gamma`, `color`, `contrast`,
  `decay`, `noise`, `couple`, `external`, `inject`
- Ping-pong FBO pair, RGBA16F internal format
- V4L2 camera input (`camera.cpp` / `camera.h`)
- GLFW windowing + GLEW loader, OpenGL 4.6 core
- Fullscreen triangle, basic keybindings (F1–F10 layer toggles, 1–5
  pattern select, QWERT param adjustments, Esc)
- Makefile builds clean with `apt install libglfw3-dev libglew-dev` on
  Debian/Ubuntu

### Build
```bash
sudo apt install libglfw3-dev libglew-dev
make
./feedback
```

Run from this directory so `shaders/` path resolves.

---

## What is MISSING compared to Windows

These features exist in the Windows version but have not been ported back
to Linux. Ordered roughly by how much engineering it takes to port each:

### Shader-only additions (simple ports)
1. **`layers/physics.glsl`** — Crutchfield-faithful camera-side physics:
   luminance inversion (Plates 6–7), sensor gamma, soft saturation knee,
   RGB cross-coupling. The Windows `.glsl` is pure GLSL with no Windows-
   specific code; copy it directly into `shaders/layers/`.
2. **`layers/thermal.glsl`** — Heat-shimmer to turbulence-vortex UV
   displacement. Tier-1 noise-based; applied between warp and optics in
   the pipeline. Same story: copy directly.
3. **Runtime quality toggles in `optics.glsl` and `noise.glsl`** — the
   Windows versions have `uBlurQuality` (5/9/25-tap), `uCAQuality`
   (3/5/8 samples), and `uNoiseQuality` (white vs. pink 1/f) switchable
   at runtime via uniforms. Port the uniforms and the branching logic.
4. **Extended couple layer** — the Windows version supports up to 4
   coupled fields with ring topology and mirrored symmetry breaking,
   controlled by `--fields` CLI flag. This tree has 2-field coupling
   only. Requires host changes (see below).

### Host-code additions (moderate ports)
5. **Orchestrator `main.frag`** — add uniforms and layer calls for
   physics and thermal. Diff from current `main.frag` vs Windows'
   `main.frag` is ~30 lines of uniform declarations + 2 new
   `if (uEnable & L_X)` calls.
6. **CLI argument parsing** — Windows has `--sim-res`, `--display-res`,
   `--fullscreen`, `--precision 16|32`, `--blur-q`, `--ca-q`,
   `--noise-q`, `--fields`, `--iters`, `--vsync`, `--rec-fps`. This tree
   has none of these — everything is compile-time constants.
7. **RGBA32F precision option** — the Windows build supports 32-bit float
   per channel optionally; this tree is RGBA16F only. One uniform + one
   texture format change, but needs a CLI flag to toggle.
8. **Preset system** — save/load regimes as `.ini` files in `./presets/`,
   cycle with Ctrl+N/Ctrl+P, save with Ctrl+S. 5 bundled starter presets
   in Windows. `~300 lines of host code`.
9. **Overlay/HUD with `stb_easy_font`** — real-time stats, layer
   toggle indicator, help screen on `H`, dynamic parameter display. 
   `overlay.h` / `overlay.cpp` + `stb_easy_font.h`. ~400 lines.
10. **Hot-reload shaders** — backslash key re-reads all `.glsl` files
    from disk and rebuilds the program without restarting. This is the
    single most useful live-coding feature. ~50 lines.
11. **Fine step sizes** — per-parameter tuned increments with Shift-for-
    coarse modifier (50× finer zoom than this tree, 50× finer rotation,
    etc.). Already-worked-out values in the Windows `key_cb`.

### Recording subsystem (largest port)
12. **`recorder.h` / `recorder.cpp` + `exr_write.h`** — Lossless EXR-
    sequence recording with fence-async PBO readback, shared GL context
    on a writer thread, cadence gate, bounded queue with trylock. Plus
    a post-exit ffmpeg encode wizard. ~600 lines total.
13. **Self-contained EXR writer** — uncompressed RGBA half-float, no
    library dependency. Pure C; ports to Linux unchanged.
14. **Post-exit encode prompt** — scans recording directories from this
    session, shows sizes and estimates, offers ffmpeg presets (source-res
    CRF18, source-res CRF23, 1080p CRF23, 1080p CRF28, skip) with sticky
    default for batch encoding.

### CRT-accurate features considered but not implemented anywhere yet
- Raster scan interlace artifacts (Crutchfield paper Fig. 2)
- Phosphor persistence with spatial anisotropy
- Proper CRT gamma inside the loop (not just gamma layer)

---

## How to sync: suggested order

If you want to bring this Linux tree up to current Windows parity, I'd
tackle it in this order. Each step is independently useful.

**Phase 1 — Shader parity (1 hour)**
Copy `physics.glsl` and `thermal.glsl` from the Windows tree into
`shaders/layers/`. Add their uniforms and conditional calls to
`main.frag`. Add `L_PHYSICS` and `L_THERMAL` bits to the host's enable
bitmask. Add keybindings (Windows uses Insert + PageDown).

**Phase 2 — Recorder (1 day)**
The recorder code is pure C++17 and OpenGL 4.6, no Windows-specific API.
Copy `recorder.h`, `recorder.cpp`, `exr_write.h` verbatim. The one thing
that differs: Windows uses `_popen`/`_pclose`, Linux uses `popen`/`pclose`
— already `#ifdef`ed in the source, so no change needed. Add the
post-exit encode prompt from Windows `main.cpp` (the `encode_prompt`
function + helpers, wrapped in an anonymous namespace).

**Phase 3 — Overlay (0.5 day)**
Copy `overlay.h`, `overlay.cpp`, `stb_easy_font.h` verbatim. Wire in the
overlay's `draw()` call in the main render loop after the blit but before
swap. Add the help-text builder.

**Phase 4 — Presets (0.5 day)**
Copy the preset save/load/cycle code from Windows `main.cpp`. Create
`presets/` directory. Bundle the 5 starter presets from the Windows tree.

**Phase 5 — Multi-field coupling (0.5 day)**
Extend the existing 2-field coupling to support up to 4 fields with ring
topology. Requires allocating more FBO pairs and updating the couple
layer to sample from the appropriate neighbor.

**Phase 6 — CLI flags + quality toggles + fine step sizes (0.5 day)**
Port the CLI argument parser, the runtime-switchable optics/noise
quality, and the fine step sizes.

Total: roughly 3–4 days of focused work to reach full parity.

---

## Why this tree wasn't kept in sync

Pragmatic reason: the Windows version is where all the iteration and
user testing has happened. Each feature was driven by a specific
observation made while running the Windows build. Keeping three trees in
sync during rapid iteration would have cost more than it was worth.

Honest reason: the original plan was "cross-platform from day one," but
in practice features got added to Windows as soon as they were needed,
and there was no forcing function to pull them back to Linux. This is a
common failure mode for cross-platform projects and this one didn't
escape it.

---

## Files to reference when syncing

All in the current Windows tree:
- `main.cpp` — ~1700 lines, the master reference for host behavior
- `recorder.cpp` / `recorder.h` / `exr_write.h` — recording subsystem
- `overlay.cpp` / `overlay.h` / `stb_easy_font.h` — HUD
- `shaders/main.frag` — the current orchestrator with all 12 layers
- `shaders/layers/physics.glsl`, `shaders/layers/thermal.glsl` — new
  shaders not present in this tree

The philosophy of the instrument is in `PHILOSOPHY.md`, included in this
zip. The academic citation trail is in `CREDITS.md`, also included. Both
apply to all three platforms equally.
