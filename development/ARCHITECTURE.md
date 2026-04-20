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
├── main.cpp             ~2500 lines. Window, main loop, CLI, state, apply_action.
├── input.cpp/h          ~750 lines. Action registry, binding table, kbd/pad/MIDI dispatch.
├── recorder.cpp/h       ~400 lines. 3-stage capture pipeline.
├── overlay.cpp/h        ~500 lines. HUD + drill-down help panel + section legend.
├── camera.cpp/h         ~300 lines. Windows Media Foundation webcam.
├── music.cpp/h          ~550 lines. Embedded JS runtime + pattern scheduler +
│                        preset manager + fb.* scalar bridge.
├── audio.cpp/h          ~500 lines. miniaudio backend, voice pool, sampler,
│                        synth voices, biquad LPF/HPF, delay bus, Freeverb.
├── js/engine.js         ~280 lines. Clean-room Strudel-syntax pattern engine
│                        (Pattern class, mini-notation parser, combinators).
├── music/*.strudel      User-editable pattern presets (5 shipped + metronome).
├── samples/*.wav        Optional user drum pack — overrides synth fallbacks.
├── exr_write.h          Self-contained EXR writer (half-float ZIP + none).
├── stb_image_write.h    Vendored (PNG screenshot).
├── stb_easy_font.h      Vendored (overlay text rendering).
├── vendor/
│   ├── quickjs/         QuickJS-ng JS runtime (MIT, pure C, ES2020).
│   └── miniaudio.h      Single-header audio library (MIT/public-domain).
├── shaders/
│   ├── main.vert        Full-screen triangle.
│   ├── main.frag        Orchestrator — #includes every layer.
│   ├── common.glsl      Shared helpers (rgb2hsv, hash, etc.).
│   ├── blit.frag        Sim FBO → display FBO.
│   └── layers/*.glsl    One layer per file. Authoring target.
│       Notable: vfx_slot.glsl (V-4 effects), output_fade.glsl (master fade).
├── bindings.ini         Auto-written on first run. Per-action key/pad/MIDI map.
├── presets/*.ini        Curated (01..05) + user-saved (auto_*).
├── research/            Source papers, V-4 inventory, A/B-bus holdback doc.
├── gallery/             README screenshots.
├── linux/, macOS/       Platform ports — transform scripts + native camera backend. See ADR-0014.
├── Makefile             MSYS2/MinGW build + `make dist` target.
├── build_msvc.bat       MSVC/vcpkg alternate (less maintained).
└── development/         You are here.
    └── plans/           Forward-looking planning docs.
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
everything else operates on the returned color. Full per-layer
reference, order rationale, and hard-vs-soft reorder constraints live
in [LAYERS.md](LAYERS.md). Don't reorder without reading it.

**Hot reload:** `\` key triggers `reload_shaders()` in main.cpp, which
rebuilds the program from disk. If compile fails, the old program
keeps running and the error is printed. Live-editable.

### Host state (main.cpp)

**The `State S` singleton** holds everything the app tracks:
- Window size, sim size, fullscreen flag.
- Array of up to 4 `FBO` structs (each a ping-pong pair).
- `Params p` — every per-layer parameter as a named field, including
  V-4 effect slot state (`vfxSlot[2]`, `vfxParam[2]`, `vfxBSource[2]`),
  output fade, and BPM fields (`bpm`, `beatOrigin`, `divIdx`,
  `bpmSyncOn`, `bpmInject`, `bpmStrobe`, `bpmVfxCycle`, `bpmFlash`,
  `bpmDecayDip`, plus transient `flashDecay` and `decayDipTimer`).
- `enable` bitfield for active layers.
- Runtime quality picks (blur kernel, CA samples, noise type).
- Cursor-navigation fields (`armedLayer`, `armedQuality`) for the
  gamepad-driven cursor in Layers and Quality help sections.
- Recorder, Overlay, Camera instances.
- Demo-mode timers.
- Screenshot-pending flag.

**The `Cfg g_cfg` struct** is the parsed-once CLI configuration.
Values flow from `g_cfg` → `State S` at startup, never the other way.
If you add a CLI flag, add a `Cfg` field, parse it in `parse_cli()`,
and apply it to `S` near the top of `main()` where the other
applications happen.

### Input system (input.cpp/h)

Keyboard, Xbox gamepad, and future MIDI all funnel through a single
registry. See [ADR-0007](ADR/0007-action-registry-and-bindings-ini.md)
for the why.

- **`ActionId` enum** — ~120 entries. Each is one thing the app can
  do (`warp.zoom+`, `vfx1.param-`, `bpm.tap`, …). Stable names, used
  both in `bindings.ini` and the help panel.
- **`Binding`** — `{action, source, code, modmask, scale, invert,
  deadzone, absolute, context}`. Sources: `SRC_KEY`, `SRC_GAMEPAD_BTN`,
  `SRC_GAMEPAD_AXIS`, `SRC_MIDI_CC`. Context drives the contextual
  gamepad model (see ADR-0009).
- **`Input::installDefaults()`** — factory map that preserves every
  pre-refactor keyboard assignment.
- **`bindings.ini`** — written next to the exe on first run. Overrides
  by `(action, source, context)`. Unknown entries logged and skipped.
- **`Input::onKey`** — GLFW key callback forwards here; dispatches
  actions by looking up matching bindings. Shift is always the 20×
  coarse multiplier (not part of the modmask).
- **`Input::pollGamepad`** — called per frame from the main loop.
  Polls Xbox-standard 15 buttons + 6 axes via GLFW, edge-detects
  buttons for DISCRETE/TRIGGER, integrates axes (RATE) or dispatches
  absolute (when `absolute=true`, for self-centering sticks like
  output fade).
- **`Input::pollMidi`** — stub today, implemented in the pending
  Strudel work. The binding table already accepts `[midi]` entries.

**`apply_action(ActionId, float magnitude)` in `main.cpp`** is the
single source of truth for state mutation. Keyboard, gamepad, and
(eventually) MIDI all dispatch here. Adding a new user action is:

1. Add an `ActionId` entry + table row.
2. Add a default binding (keyboard and/or gamepad) in
   `Input::installDefaults`.
3. Add a `case` in `apply_action` for the mutation.
4. Add a row to the relevant section builder in `main.cpp` so the
   help panel picks it up.

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

Three distinct on-screen surfaces:

1. **HUD toasts (bottom-left)** — cumulative per-key parameter
   changes and one-shot events, auto-fade after ~5s.
2. **Drill-down help panel (top-left)** — menu of 16 sections;
   Enter/Up/Down/Esc to navigate; each section renders a live body
   (current values) + a legend (current gamepad map). Does NOT dim
   the main view — panel is semi-opaque so the feedback stays
   readable behind it. See ADR-0009 for why it became the anchor
   for contextual gamepad.
3. **Bottom-right gamepad tag** — always-visible when help is
   closed and a gamepad is connected; shows active section + "Back:
   help" reminder. Discoverability for gamepad-only users.

Integration points:
- `S.ov.logEvent("message")` — one-shot toast.
- `S.ov.logParam(key, label, delta, value)` — accumulating toast.
- `S.ov.toggleHelp()`, `S.ov.helpVisible()`, `S.ov.inSectionView()`.
- `S.ov.setHelpSections(names)` — one-time at startup, ordered list.
- `S.ov.setHelpProvider(fn)` / `setLegendProvider(fn)` — callbacks
  that return up-to-date body text / legend strings per section.
- `S.ov.activeSection()` / `setActiveSection(i)` — tracks which
  section the gamepad is bound to. Defaults to `Warp` (index 2) on
  startup.
- `S.ov.helpUp()` / `helpDown()` / `helpEnter()` / `helpBack()` —
  navigation primitives called from `apply_action` when help is
  visible.

The body and legend providers are invoked every frame the section
is shown so live values stay current. Section builders live in
`main.cpp` next to the `help_section_body` dispatcher.

**Text rendering** uses `stb_easy_font.h`. Panel sizing scales with
window: 380×420 at 720p up through 540×620 at 4K. A separator line
divides body from legend. The help index lives in `HELP_SECTIONS[]`
with an invariant mapping onto `CTX_SEC_STATUS + N` in the input
layer (see `input.h::BindContext`).

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

### Music + audio engine

**Files:** `music.cpp/h`, `audio.cpp/h`, `js/engine.js`, `music/*.strudel`.

**Purpose:** native pattern-driven audio so feedback makes music without
requiring an external DAW, and so live visual state can modulate the
music in real time (the "closed loop" use case). Strudel syntax is
supported through a clean-room JS reimplementation — see ADR-0012 for
why not to embed Strudel itself.

**Three layers:**

1. **JS runtime (QuickJS)** — `music.cpp::init()` spins up a JSRuntime
   + JSContext at startup, loads `js/engine.js` once. The engine
   exposes `Pattern`, `s()`, `note()`, `stack`, `cat`, mini-notation
   `parseMini`, and chainable effect setters (`.lpf`, `.delay`,
   `.room`, `.attack`, `.release`, …). See ADR-0010.
2. **Scheduler (music.cpp)** — `Music::update(now, dt, bpm)` runs once
   per render frame. Advances a cycle clock by frame `dt` (not wall
   time — see ADR-0013). Each frame queries the active pattern over
   a ~250 ms lookahead window, dedupes events by `hap.whole.begin`,
   translates each event's onset into a wall-clock delay, and pushes
   it to the audio module's pending queue.
3. **Audio engine (audio.cpp)** — miniaudio callback thread runs a
   24-voice pool at 48 kHz stereo. Each voice is either a sample
   (WAV/FLAC/MP3 via ma_decoder) or a synth (sine/saw/square/tri
   with ADSR). Per-voice biquad LPF + HPF; global delay and reverb
   busses with per-voice send amounts.

**Preset lifecycle:**
- `Music::scanPresets("music")` sorts `*.strudel` files alphabetically.
- `Music::loadPreset(i)` reads the file fresh each time and sets the
  active pattern string. First preset auto-loads at boot.
- `Music::pollPresetReload()` stat()s the active file every ~250 ms;
  on mtime change it re-reads and re-sets. Hot-reload for live edits.
- `Music::pushMomentaryPreset("breakbeat")` / `popMomentaryPreset()`
  power the hold-Space live gesture — switches to a named preset for
  the duration of a press and restores on release.

**Video ↔ music bridge:** `Music::setScalar(name, value)` updates a
property on the JS `fb` global. `main.cpp` publishes 12 scalars per
frame (`fb.zoom`, `fb.theta`, `fb.decay`, `fb.contrast`, `fb.chroma`,
`fb.blur`, `fb.noise`, `fb.inject`, `fb.outFade`, `fb.paused`,
`fb.beatPhase`, `fb.hueRate`). Patterns read them as plain numbers.

**MIDI integration** (separate from the native engine but part of the
music story): `Input::pollMidi()` in `input.cpp` registers
feedback.exe as a virtual MIDI *input* port via the teVirtualMIDI
driver (`virtualMIDICreatePortEx2`). Strudel's Web MIDI output sees
the port, sends Clock / Start / Stop / Note-On / CC. MIDI Clock drives
BPM when live. See ADR-0011.

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
| Add a new CLI flag | `Cfg` struct, `parse_cli`, help text, `State S` application in `main()`. |
| Add a new user action | `ActionId` entry in `input.h`; table row in `input.cpp::ACTIONS[]`; default binding in `Input::installDefaults`; `case` in `apply_action` in main.cpp; row in the relevant `section_*` builder. |
| Rebind an existing action | Edit `bindings.ini` next to the exe — no recompile. |
| Add a new V-4 effect | New `if (eff == N)` branch in `shaders/layers/vfx_slot.glsl`; extend `VFX_NAMES[]` in main.cpp; bump `VFX_COUNT`. |
| Change recording format | `exr_write.h` + `recorder.cpp` encoder loop. |
| Add another CPU capture card | `camera.cpp` (Media Foundation). |
| Port to Linux/macOS | `linux/` and `macOS/` subdirs (separate main.cpp currently). |
| Add a music preset | Drop a `.strudel` file into `music/`. Edit any number at runtime and save — hot-reloads within ~250 ms. |
| Add a Pattern combinator | Add a method on `Pattern.prototype` in `js/engine.js`. For a no-op safety net instead of real logic, add the name to `_UNIMPL_METHODS`. |
| Add a new audio effect | Extend `audio.cpp` with DSP + optional `Voice` fields, add `TriggerOpts`/`NoteOpts` field, marshal from `Event` in `music.cpp`'s scheduler. |
| Publish a new video→music scalar | Call `Music::setScalar("name", val)` each frame in `main.cpp`. Document the name in the README and the Music help section. |
| Add a MIDI-driven action | Wire the binding in `bindings.ini [midi]` (see `note:N ch=N` / `cc:N ch=N`). Existing actions work with any MIDI source. |

## Invariants the code relies on

Break these and things go subtly wrong:

1. **Every layer's `_apply()` function signature is in main.frag's dispatch.**
   If a layer function signature changes, main.frag needs updating too.
   See [LAYERS.md](LAYERS.md).
2. **Layer bits in host `L_*` enum match the GLSL `const int L_*`.**
   These are mirrored by hand. Getting them out of sync silently breaks
   layer toggles. See [LAYERS.md](LAYERS.md).
2a. **No layer in the default feedback path clamps to [0,1].** The loop
   is float precisely so HDR overshoot (S-curves, Reinhard pre-knee,
   additive bloom) is preserved. Use `max(x, 0)` only as NaN defense,
   never as range compression. See
   [ADR-0015](ADR/0015-pipeline-order-and-float-preservation.md) and
   [LAYERS.md §float-precision invariant](LAYERS.md#float-precision-invariant).
3. **`S.p` is the single source of truth for per-layer parameters.**
   Uniforms read from it; presets write from/to it; `apply_action`
   modifies it.
4. **Sim FBO never has the overlay drawn on it.** Recording and
   screenshots go from the sim FBO, so the overlay stays display-only.
5. **The recorder runs lockstep with the render thread for capture
   ONLY.** Subsequent stages are decoupled — the render thread
   doesn't block waiting on disk.
6. **`CTX_SEC_STATUS + N == section N` in HELP_SECTIONS[].** The
   input layer's context enum and the overlay's section index use the
   same ordinal. Reordering `HELP_SECTIONS[]` breaks gamepad dispatch
   silently. The array has 17 entries as of v0.1.3 (Music added
   between BPM and Quality).
7. **`apply_action` is the only writer to `S.p` in response to user
   input.** Keyboard, gamepad, MIDI, overlay navigation — all go
   through it. If you add a side path (e.g. a demo scheduler), call
   `apply_action` rather than mutating directly so HUD toasts and
   downstream effects stay consistent.
8. **Beat-driven transients (`flashDecay`, `decayDipTimer`) compose
   into render-time `effOutFade` / `effDecay` in `render_field`.**
   The user's `p.outFade` and `p.decay` are the setpoints; BPM
   modulations ride on top without mutating them. Don't confuse the
   two in preset save/load.
9. **The music scheduler fires on `hap.whole.begin`, not `part.begin`.**
   Strudel's queryArc returns every hap whose *part* overlaps the
   query arc. Firing on `part.begin` re-triggers every frame the
   window slides across a hap — producing dense "avant garde"
   noise. The scheduler filters to events where `whole.begin ∈
   [qStart, qEnd)`. Don't change this back.
10. **The music cycle clock advances on frame `dt`, not wall time.**
    Wall-time advancement plus the OS throttling the render thread
    during alt-tab produced huge cycle jumps and event bursts on
    refocus. Dt-coupling also gives us "sim slows → music slows"
    for free. Clamp dt to 100 ms per frame; anything bigger is an
    execution stall we should skip, not replay.

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
