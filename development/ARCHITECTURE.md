# ARCHITECTURE — how Crutchfield Machine is put together

This doc describes *mechanism*. For *intent*, see [DESIGN.md](DESIGN.md).

## 30-second overview

```
┌──────────────┐  uniforms   ┌───────────────┐  GL calls  ┌──────────┐
│ CLI / picker │────────────▶│ State S (main)│───────────▶│  Shader  │
└──────────────┘             └───────┬───────┘            │ pipeline │
                                     │                    │  (GLSL)  │
       ┌─────────────────────────────┼────────────────────┴──────────┘
       │                             │             ┌──────┐
       │                             ├────────────▶│Overlay│
       │                             │             └──────┘
       │                             │             ┌──────┐
       │                             ├────────────▶│  Cam │ (Media Foundation)
       │                             │             └──────┘
       │                             │             ┌──────┐
       │                             └────────────▶│  Rec │
       │                                           └──┬───┘
       │                                              │
       │    sim FBO  ┌─────────┐   heap buf   ┌───────┴────┐
       └────────────▶│ readers │─────────────▶│  encoders  │──▶ EXR files
                     │  (GL)   │              │  (pure CPU)│
                     └─────────┘              └────────────┘
```

Main thread owns GL and pumps the render loop. The recorder has its own
thread pools. Everything else is single-threaded.

## Top-level file map

```
feedback-GBPS/
├── main.cpp             ~2100 lines. Window, main loop, input, CLI, state.
├── recorder.cpp/h       ~400 lines. 3-stage capture pipeline.
├── overlay.cpp/h        ~400 lines. HUD + help overlay, stb_easy_font.
├── camera.cpp/h         ~300 lines. Windows Media Foundation webcam.
├── exr_write.h          Self-contained EXR writer (half-float ZIP + none).
├── stb_image_write.h    Vendored (PNG screenshot).
├── stb_easy_font.h      Vendored (overlay text rendering).
├── shaders/
│   ├── main.vert        Full-screen triangle.
│   ├── main.frag        Orchestrator — #includes every layer.
│   ├── common.glsl      Shared helpers (rgb2hsv, hash, etc.).
│   ├── blit.frag        Sim FBO → display FBO.
│   └── layers/*.glsl    One layer per file. Authoring target.
├── presets/*.ini        Curated (01..05) + user-saved (auto_*).
├── research/            Source papers + PHILOSOPHY.md.
├── gallery/             README screenshots.
├── linux/, macOS/       WIP ports (see their READMEs).
├── Makefile             MSYS2/MinGW build + `make dist` target.
├── build_msvc.bat       MSVC/vcpkg alternate (less maintained).
└── development/         You are here.
```

## Components

### Shader pipeline (the core)

**Files:** `shaders/main.frag` orchestrates; `shaders/layers/*.glsl`
implement individual layers; `shaders/common.glsl` provides helpers;
`shaders/blit.frag` is the final display copy.

**How it runs:**
1. Host loads `main.frag` at startup, resolves `#include` directives
   manually (not via any GLSL preprocessor), compiles once.
2. Per frame, host binds the previous-frame FBO as `uPrev`, the
   partner-field FBO as `uOther`, and the camera texture as `uCam`.
3. `main.frag` walks its dispatch list (lines ~90–140), calling each
   layer function only when its enable bit is set in `uEnable`.
4. Output writes to a ping-pong FBO; next frame, roles swap.
5. After all sim iterations (`--iters N`, default 1), `blit.frag` copies
   the field-0 FBO to the default framebuffer for display.

**Layer dispatch order is significant.** Warp and thermal happen on
the *sample* UV (before the texture read); optics performs the read;
everything else operates on the returned color. Don't reorder without
understanding what each stage expects.

**Hot reload:** `\` key triggers `reload_shaders()` in main.cpp, which
rebuilds the program from disk. If compile fails, the old program
keeps running and the error is printed. Live-editable.

### Host state (main.cpp)

**The `State S` singleton** holds everything the app tracks:
- Window size, sim size, fullscreen flag.
- Array of up to 4 `FBO` structs (each a ping-pong pair).
- `Params p` — every per-layer parameter as a named field.
- `enable` bitfield for active layers.
- Runtime quality picks (blur kernel, CA samples, noise type).
- Recorder, Overlay, Camera instances.
- Demo-mode timers.
- Screenshot-pending flag.

**The `Cfg g_cfg` struct** is the parsed-once CLI configuration.
Values flow from `g_cfg` → `State S` at startup, never the other way.
If you add a CLI flag, add a `Cfg` field, parse it in `parse_cli()`,
and apply it to `S` near line 1955 where the other applications happen.

**Key callback** (`key_cb` in main.cpp) is the dispatch table for
keyboard input. Structured as: layer toggles → modifier combos
(Ctrl+S/N/P) → parameter adjustments. Additions go in the appropriate
section; don't create a new callback.

### Recorder pipeline

**Files:** `recorder.cpp`, `recorder.h`. Self-contained.

**Three-stage pipeline** (see [ADR-0005](ADR/0005-three-stage-recorder-pipeline.md)):

1. **Capture** (main render thread): `glReadPixels` from the sim FBO
   into one of N_PBO=8 pixel-buffer-objects, followed by a fence.
   Pushes `(pbo, fence)` onto `queue_`. Drops the frame if the ring is
   saturated.
2. **Readers** (N_READBACK=4 threads, each with a shared GL context):
   wait on fence, map PBO, copy (and convert float→half if precision
   is 32) into a heap buffer from `bufPool_`. Unmap PBO. Push buffer
   onto `encodeQueue_`.
3. **Encoders** (`encoderThreads` pure-CPU threads): pop from
   `encodeQueue_`, call `exr::write_rgba_half` with the chosen
   compression, return buffer to the pool.

**The RAM pool** (`bufPool_`, configurable via `--rec-ram-gb`) is the
back-pressure buffer. When encoders fall behind, the pool drains;
readers block; PBOs back up; capture drops.

**Known issue:** exit-time encoder drain blocks the main thread for
up to ~30s with no UI feedback. See TODO.md.

### Overlay / HUD

**Files:** `overlay.cpp`, `overlay.h`.

Renders bottom-left toast lines (cumulative per key, auto-fade after
~6s) and a full-screen help screen (toggled with H). Uses
`stb_easy_font.h` for text.

Integration points:
- `S.ov.logEvent("message")` — one-shot toast.
- `S.ov.logParam(key, label, delta, value)` — accumulating toast.
- `S.ov.toggleHelp()`, `S.ov.helpVisible()`, `S.ov.setHelpText(s)`.

The help text is rebuilt every frame it's visible (see
`build_help_text()` at main.cpp:~870) so live values stay current.

### Preset system

**INI format.** Human-readable, section-based. `preset_write()` and
`preset_load()` at main.cpp:~510 and :~630. Unknown keys/sections are
tolerated on load so old presets don't break when we add new params.

**Lifecycle:**
- `Ctrl+S` calls `preset_save_now()` → writes `presets/auto_<ts>.ini`.
- `Ctrl+N` / `Ctrl+P` cycle through alphabetically-sorted files.
- `--preset NAME` at launch resolves via `preset_resolve()` (bare
  stem, filename, or path).
- Picker's "Load preset" option also uses `preset_load()`.

Curated presets (`01_*.ini` through `05_*.ini`) are hand-authored and
committed. Auto-saves (`auto_*.ini`) are user-generated; git-ignored
only if you delete them.

### Camera input (Windows only)

**Files:** `camera.cpp`, `camera.h`.

Media Foundation capture of the first video device offering NV12, YUY2,
or RGB24. Converts to RGB and uploads to `S.camTex` each frame via
`S.cam.grab()` in the main loop. If no camera or no compatible format,
the `external` layer becomes a no-op. Graceful degradation.

### Distribution build (Makefile `dist` target)

See [RUNBOOK.md](RUNBOOK.md) for the full procedure. Key: static links
everything MSYS2-provided (glfw3, glew32, zlib, winpthread, libstdc++)
so the resulting zip runs on a fresh Windows box. Import-table check
is part of the target.

## Data flow per frame

```
                                     ┌─ glfwPollEvents (input → State)
                                     │
main loop iteration                  ├─ demo tick (auto cycle presets,
(main.cpp ~1962 while-loop)          │    auto inject)
                                     │
                                     ├─ for each active field:
                                     │    for each iter (default 1):
                                     │      bind field[i] write FBO
                                     │      bind field[i] read texture as uPrev
                                     │      bind field[other] as uOther
                                     │      glDrawArrays (main.frag runs)
                                     │
                                     ├─ blit field[0] → default framebuffer
                                     │
                                     ├─ rec.capture(field[0].fbo)  [if recording]
                                     │   → PBO + fence → queue_
                                     │
                                     ├─ save_screenshot(field[0].fbo)
                                     │   [if PrtSc was pressed]
                                     │
                                     ├─ overlay.draw (text on top)
                                     │
                                     ├─ glfwSwapBuffers
                                     │
                                     └─ frame pacing (sleep + spin to --fps)
```

The recorder's reader and encoder threads run independently; the main
loop doesn't wait on them unless explicitly draining at exit.

## Performance budget

Targeting **4K60** on RTX 3090-class hardware:

| Stage | Budget | Notes |
|---|---|---|
| Shader pass (all 12 layers, iters=1) | ~5-8 ms | Most expensive: 25-tap Gaussian blur, 8-sample chromatic aberration. |
| Blit pass | <0.5 ms | Trivial copy. |
| Recorder capture (PBO + fence) | <0.5 ms | Async; render thread returns immediately. |
| Overlay draw | <0.5 ms | Bounded — help screen text is cached. |
| Frame pacing overhead | 2 ms spin | Deliberate — vsync + uncapped drifts off target. |

**If the render loop exceeds ~16 ms** (60 fps budget), the most
common culprits are: `--iters` >2, `--fields` 4 with heavy layers,
4K + `--precision 32` on weaker GPUs. Drop `--precision` to 16 first,
then lower `--iters`, then lower sim resolution.

## Extension points

| You want to... | ...touch this |
|---|---|
| Add a new visual behavior | `shaders/layers/<name>.glsl` + wiring in `main.frag` + host-side uniform plumb in `main.cpp`. See [CONTRIBUTING.md](../CONTRIBUTING.md). |
| Add a new CLI flag | `Cfg` struct, `parse_cli`, help text, `State S` application near line 1955. |
| Add a new key binding | `key_cb` in main.cpp, preserving the section structure (layer toggle / action / modifier / parameter). |
| Change recording format | `exr_write.h` + `recorder.cpp` encoder loop. |
| Add another CPU capture card | `camera.cpp` (Media Foundation). |
| Port to Linux/macOS | `linux/` and `macOS/` subdirs (separate main.cpp currently). |

## Invariants the code relies on

Break these and things go subtly wrong:

1. **Every layer's `_apply()` function signature is in main.frag's dispatch.**
   If a layer function signature changes, main.frag needs updating too.
2. **Layer bits in host `L_*` enum match the GLSL `const int L_*`.**
   These are mirrored by hand. Getting them out of sync silently breaks
   layer toggles.
3. **`S.p` is the single source of truth for per-layer parameters.**
   Uniforms read from it; presets write from/to it; key handlers modify it.
4. **Sim FBO never has the overlay drawn on it.** Recording and
   screenshots go from the sim FBO, so the overlay stays display-only.
5. **The recorder runs lockstep with the render thread for capture
   ONLY.** Subsequent stages are decoupled — the render thread
   doesn't block waiting on disk.

## Where the system is brittle

- **Shader hot-reload** works but errors go to stderr — no in-window
  indication when the reload failed. See TODO.md.
- **Layer dispatch order in main.frag** is hand-maintained. No
  automation catches "you added a layer but forgot to dispatch it."
- **Windows-only camera path.** `camera.cpp` uses Media Foundation; the
  Linux stub uses V4L2 with a separate file; macOS has nothing yet.
- **Preset schema drift.** New params must be added to both read and
  write sides of the preset code; forgetting the write side silently
  drops data.
