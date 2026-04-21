# Worklog — 2026-04-21 polish + release prep

Follow-up after 2026-04-20 extended session. Small corrections and a
release cut.

- Fixed binding collision: `Ctrl+Alt+V` was bound to both
  `ACT_BPM_VFXCYCLE_TOGGLE` and the new `ACT_BPM_INVERT_TOGGLE`; the
  dispatcher matched the first, so beat-invert was silently
  unreachable. Moved beat-invert to `Ctrl+Alt+R` (`R`everse).
- Split `CHEAT_SHEET.md` (real one-page print sheet, ~60 lines)
  from `REFERENCE.md` (full user reference, ~450 lines). README
  links to both with the distinction explained.
- Silenced the noisy `[music] pattern set: <full body>` stdout
  dump on every preset load. Preset-name line already carries the
  info; the body dump was filling the terminal.
- Added `--high-color` / `--hi-color` CLI flag: windowed,
  `--precision 32 --blur-q 2 --ca-q 2 --fields 4`. Discoverable as
  picker option `8  High color`. Complements fullscreen max path
  for OBS-capture workflows.
- Removed the bottom-left HUD toast backing rect. White text floats
  directly over the live feedback — legible enough against the
  moving image, and one less opaque square on the display.

Release: cut **v0.1.4** with Windows and Linux artifacts.

---

# Worklog — 2026-04-20 extended session (performance-layer expansion)

Continuation of the same day. Scope was initially a small colour-quality
pass; grew into a broad performance-layer expansion once the pipeline
rework exposed how much more expressive room the float-preserved path
could carry.

## Shader-layer additions

- **Pixelate** (new, `shaders/layers/pixelate.glsl`). In-loop spatial
  quantizer. When active (`uPixelateStyle != 0`) it **replaces** the
  sample stage (takes over from `optics_sample`). 9 styles = 3 shapes
  (dots / hard squares / rounded squares) × 3 sizes (small / med /
  large). Cell sizes scale with sim resolution (`uRes.x / 1280` reference)
  so "small" reads the same at 720p and 4K. Cell centres re-apply
  warp + thermal so pixelated content propagates through the dynamics
  instead of freezing into a static grid. See ADR-0017.
- **Pixelate CRT bleed** (inside the same layer). Orthogonal axis to
  style — a 6-entry cycle (off / soft / CRT / melt / fried / burned),
  each with a *dominant* aesthetic (not gradual same-axis variation).
  - `off` rigid
  - `soft` edge glow only
  - `CRT` vignette + chromatic convergence error (Trinitron feel)
  - `melt` sinusoidal jitter — pixels swim
  - `fried` time-varying chromatic drift + strobing vignette
  - `burned` seed-tinted dead-pixel groups; alpha ~7%; reseed rerolls
- **Noise archetypes** extended from 2 → 5: `white`, `pink 1/f`,
  `heavy static`, `VCR rolling bar`, `dropout`. Amplitudes tuned so
  each reads at default `uNoise ≈ 0.002`.
- **Pattern sparsity** via alpha channel: `pattern_gen()` now returns
  `vec4` (rgb + alpha). Most existing patterns return alpha 1.0
  (no behaviour change); new animated / sparse patterns use alpha 0
  for non-content pixels so the feedback field continues evolving
  around them.
- **Inject patterns 5-9**: noise field, concentric rings, spiral,
  polka dots, starburst (keys `6-0`).
- **Inject pattern 10 — bouncer**: 1/20-cell quantized pong ball,
  triangle-wave bounce, ~2 s period. Alt+B triggers a 10-second hold
  (`injectHoldTimer` bypasses normal inject fade).

## Host / audio

- **Music → visual trigger bridge** (`audio.cpp`). Each
  `Audio::trigger()` and `trigger_note()` classifies the event into
  kick / snare / hat / bass / other. Host drains
  `consumeTriggerPulses()` per frame into decaying envelopes
  (0.88 per frame), published as `uMusKick/Snare/Hat/Bass/Other`
  uniforms. Noise dropout mode reads these and produces **per-bucket
  glitch flavours** — kick = wide black holes, snare = white flashes,
  hat = tiny green speckle, bass = rotating-hue blocks, other =
  rainbow glitches. See ADR-0018.
- **Beat-driven hue jump** (6th BPM modulation). Each beat kicks
  `hueBeatKick` to `step × 0.001`; decays 0.9/frame so the total
  rotation sums to `step/100` per beat, distributed across ~20
  frames. Step range 0-100 with progressive nudge. On by default
  at step 12 (1/8 rotation per beat).
- **Beat-driven invert flip** (7th BPM modulation). Every Nth beat,
  flips `p.invert`. Default div = 4 (once per bar in 4/4). Pairs
  with the frame-level `invertPeriod` that still controls *how often*
  the invert op runs once invert is "on".
- **Invert frame-divider** fix for the 60 Hz seizure-strobe.
  `uInvertPeriod` defaults to 20 (3 Hz flip instead of 60 Hz).
- **Boot-time random inject**. After preset load, pick one of 11
  patterns and set `inject = 1.0`. Normal fade takes it out over
  ~20 frames; no more black-screen start.
- **Pause ↔ music coupling**. `P` now pauses both visuals and music
  together, remembering pre-pause music state so unpausing respects
  whatever the user had before.
- **Default enable = `L_ALL`**. All 12 layers on at startup. Physics
  and thermal defaults are near-identity so this doesn't explode.
- **Default BPM modulations retuned**: `bpmInject` OFF (was ON),
  `bpmHueJump` ON at step 12.
- **Brightness knob** (display-only). `uBrightness` in `blit.frag`,
  bound to `Alt+Up`/`Alt+Down`, range 0-4.0. Does NOT feed back —
  sim dynamics are unchanged, only the blit-to-window multiplies.
  Recording EXR is captured before the blit, so recordings are
  unaffected.
- **Exit confirmation**. First `Esc` arms; second `Esc` or `Y`
  quits; `N` or any other key cancels.
- **Layer-off warnings**. `apply_action` consults an `ACTION_REQS`
  table and posts a HUD hint when a parameter is nudged while its
  layer is off (e.g. "contrast set — layer off, F5 to activate").
- **Help panel redesign**. Removed the full-panel opaque dim.
  Per-line dark backing via `drawTextBacked` so feedback shows
  through between rows. Preserves legibility without hiding the
  image.

## Docs

- `development/LAYERS.md` updated: pixelate added to pipeline
  diagram as the alternate sample stage; noise archetype list; music
  bridge mention.
- `development/ARCHITECTURE.md`: music-viz bridge component;
  pixelate and brightness extension points; new invariants around
  pattern alpha and music-envelope decay ownership.
- `development/TODO.md`: marked completed items; added follow-ups
  (retune curated presets against new default-ON layers, Spout GPU
  texture share for OBS).
- ADR-0017 (pixelate as alternate sample); ADR-0018 (music → visual
  trigger bridge). ADR index updated.
- `README.md`: new keybindings, brightness, exit-confirm, new
  noise/inject/pixelate entries in the CLI flag cookbook.

## Known follow-ups

- Curated presets (`01_*`–`05_*`) were all saved before
  `L_ALL`-default + clamp-removals; their visual character shifted.
  Needs an aesthetic retuning pass (not blocking).
- Spout GPU-texture-share for live-feed to OBS at float precision.
  Scoped informally during session; not implemented.
- Exit-confirmation HUD text is just a toast; a dedicated modal
  panel would be clearer UX.

---

# Worklog — 2026-04-20 session (pipeline order + float preservation)

Short audit of `shaders/main.frag` and the layer files surfaced two
issues worth fixing before further work lands on top of them.

## Scope

- **Float-precision invariant restored.** Removed `clamp(rgb, 0, 1)`
  from the tail of `physics_apply` (physics.glsl:54) and from
  `contrast_apply` (contrast.glsl:8). Both were defensive clamps that
  silently undid the whole point of running the loop in RGBA32F.
  `max(c.rgb, 0.0)` in gamma.glsl stays — that's legitimate NaN
  defense for `pow()` of negatives, not range compression.
- **Pipeline tail reordered** in main.frag:
  - `decay` now runs BEFORE `couple` / `external`, so fresh
    partner-field and camera content enter at full amplitude against
    faded feedback (matches the analog-rig metaphor).
  - `inject` now runs BEFORE `noise`, so injected patterns pick up
    sensor-floor texture instead of reading as synthetic.
- **New canonical doc**: `development/LAYERS.md` — pipeline order
  reference, per-layer quick ref, hard-vs-soft reorder constraints,
  and the float-precision invariant written down.
- **ADR-0015** — records both the reorder and the clamp removal, and
  the aesthetic cost (every curated preset will look different;
  retuning is follow-up work).
- ARCHITECTURE.md and development/README.md cross-refs updated.

## Follow-up (not done this session)

- Retune `presets/01_*.ini` through `05_*.ini` against the new
  pipeline. Aesthetic calibration pass; no code.
- Consider whether `contrast` wants a soft-knee option (matched to
  physics's Reinhard) for users who want a high contrast setting
  without unbounded overshoot.

## Follow-up that landed same session: NaN/Inf sanitize

First live test of the unclamped pipeline revealed the expected
IEEE-754 trap: heavy contrast overshoot pushes pixels to `Inf`,
downstream arithmetic turns them into `NaN`, and NaN propagates
through every subsequent op (`decay`, `couple`, `external`, `inject`,
`noise`) because `NaN * 0 = NaN`. Symptom: "inject stopped working"
after a divergent run, recoverable only via `C` (clear fields).

Added a per-component `isnan || isinf ? 0 : v` select at the end of
`output_fade_apply` — the one and only place in the default path
where the pipeline touches a component for non-range reasons. Full
HDR range still preserved; only non-finite components get clobbered.
Uses ternary not `mix()` because `mix(NaN, 0, 1) = NaN`.

ADR-0016 captures the derivation — why output_fade, why ternary,
why 0 as the substitute, why not clamp, why not sanitize earlier.
LAYERS.md §float-precision invariant updated with the exception.

---

# Worklog — 2026-04-19 session

Polish pass aimed at making the repo usable by people other than the author.

## Scope

- Hardware recording path (Elgato 4K60 Pro MK.2) investigated and documented.
- Binary distribution: go DLL-free so prebuilt releases actually run on a
  fresh Windows box.
- Extensibility: contributor doc + shader authoring guide.
- Power-user features: `--preset`, screenshot key, auto-demo mode.
- Platform honesty: annotated `linux/` and `macOS/` as WIP.

## Changes by area

### Recorder pipeline (earlier in session, already shipped)

Split recorder into reader + encoder thread pools connected by a RAM
buffer pool. New CLI: `--rec-ram-gb`, `--rec-encoders`, `--rec-uncompressed`.
Fixes the case where EXR ZIP encoders couldn't keep up with 4K60 capture.

Commits:
- `672fda7` — Split recorder into readback + encoder stages with RAM buffer pool
- `04f38ea` — Add research/ with source papers and feature inventory

### 8-bit feedback mode

Extended `--precision` from `{16, 32}` to `{8, 16, 32}`. `--precision 8`
runs the feedback loop in RGBA8 unorm so values clamp to [0,1] every pass
and quantize to 256 levels per channel — simulates what an 8-bit HDMI
capture (Elgato, OBS window-capture) does to the *dynamics*, not just to
the output frame. Different banding/attractor behavior than float.

### HDMI capture card investigation

Detected on the system: Elgato Game Capture 4K60 Pro MK.2 (PCIe,
`VEN_12AB`, driver `SC0710.X64`). No public SDK. The card is input-only
(HDMI IN + HDMI OUT passthrough). Three documented integration paths:

1. **Cable-only**: GPU HDMI → Elgato HDMI IN → Elgato HDMI OUT → monitor.
   Elgato 4K Capture Utility or OBS records it. Zero app changes.
2. **`--display N` flag** (not yet implemented): target a specific
   monitor so fullscreen lands on the capture display automatically.
3. **`obs-websocket`** (not yet implemented): backtick key triggers OBS
   recording in parallel with EXR. ~100 lines.

OBS's Windows Graphics Capture path is essentially equivalent to the
Elgato in quality (both 8-bit) but needs no cable or extra hardware — the
leading recommendation for most users.

### Static binary / distribution

**Makefile**: added `-DGLEW_STATIC`, switched to
`-Wl,-Bstatic -lglfw3 -lglew32 -Wl,-Bdynamic` so the linker picks the
`.a` archives over `.dll.a`. System DLLs (`opengl32`, `gdi32`, `mf*`) stay
dynamic — they're part of Windows.

New `make dist` target: builds, bundles `feedback.exe + shaders/ +
presets/ + README + LICENSE + CREDITS` into `feedback-windows-x64.zip`,
and runs `objdump -p` as a sanity check that no MSYS2 DLLs snuck into the
import table. This is what gets uploaded to GitHub Releases.

### Preset CLI flag

`--preset NAME` loads a preset at startup. Accepts:
- bare stem (`03_turing_blobs`)
- filename (`03_turing_blobs.ini`)
- absolute or relative path

Non-fatal if not found (prints a warning, falls back to defaults).

### Screenshot key

`PrtSc` key writes a PNG of the current sim-resolution frame to
`./screenshots/shot_<timestamp>.png`. Reads from the displayed field's FBO
(not the display framebuffer), so screenshots are at full sim quality and
never include the HUD overlay. Uses `stb_image_write.h` (added to repo,
public domain).

### Auto-demo mode

- `--demo-presets S` — cycle to next preset every S seconds
- `--demo-inject S` — fire a randomly-chosen injection pattern every S seconds
- `--demo` — shortcut for `--demo-presets 30 --demo-inject 8`

Both timers independent; set either to 0 to disable. Intended for gallery
installations, background monitors, screen-saver-style use.

### Documentation

- **`CONTRIBUTING.md`** (new): project layout, step-by-step "add a new
  shader layer" walkthrough using a `sharpen` example, shader contribution
  ideas (sobel, bloom, kaleidoscope, bit-crush, displacement warp, bloom,
  etc.), host-side ideas (audio reactivity, MIDI, OSC), and infra ideas
  (CI, release automation).
- **`linux/README.md`** (new): marks the Linux subdir as proof-of-concept;
  lists what's missing vs. the Windows build.
- **`macOS/README.md`** (new): marks the macOS subdir as not-started;
  lists planned approach.
- **`README.md`** (root): added "Quick start — prebuilt binary" section,
  `make dist` step in the build instructions, Preset / Auto-demo sections
  in the CLI cookbook, `PrtSc` / recording / preset keys in the Controls
  table, Contributing + Background sections at the bottom.

### Housekeeping

- `.gitignore`: `screenshots/`, `feedback-windows-x64/`, `feedback-windows-x64.zip`.

## Files touched

### New
- `CONTRIBUTING.md`
- `WORKLOG.md` (this file)
- `linux/README.md`
- `macOS/README.md`
- `stb_image_write.h` (third-party, public domain)

### Modified
- `Makefile` — static linking, `dist` target
- `README.md` — prebuilt binary quickstart, expanded CLI cookbook,
  controls table, contributing link, background link
- `main.cpp` — `--precision 8`, `--preset`, `--demo*`, screenshot key,
  stb_image_write include, State fields for demo timers + screenshot
  flag, config pass-through
- `.gitignore` — screenshots, dist bundle

## Known pending / follow-ups

### Must fix before release

- **EXR recorder exit hang** — on shutdown with a full RAM buffer, stop()
  synchronously joins encoder threads while they drain to disk. Window
  goes Not Responding for up to ~30s at 4K60, looking like a crash. Fix
  requires splitting stop() into `beginStop()` (non-blocking) and
  `finishStop()`, then polling from the main loop while showing a
  "Saving recording" overlay. Scoped but not implemented this session.

### Nice-to-have (from the polish list)

- Screenshots / demo GIF in the README (needs to be recorded — can't do
  automated).
- Non-empty default startup state (load `01_default.ini` at launch).
- Troubleshooting section in README (black screen, 4K slow, recorder
  drops, no camera).
- GitHub Actions CI for cross-platform builds.
- `presets/README.md` describing the curated presets.
- `--display N` flag for targeting a specific monitor (matters for the
  Elgato/OBS workflow).
- `--record-on-start` for unattended-capture sessions with `--demo`.

### Architecture

- Preset menu overlay (in-window browser) — another contributor is
  working on this in a separate worktree per user note.
- Audio reactivity (WASAPI loopback + RMS/band envelope) — flagged as
  coming soon in CONTRIBUTING.md.

## Verification owed

Not executed in this session — user to run in their MSYS2 shell:

```bash
make clean && make
./feedback.exe --demo
./feedback.exe --preset 03_turing_blobs
# press PrtSc to screenshot

make dist
# sanity-check the objdump output: should list only Windows system DLLs
```

The bash environment available during this session had a `TMP`
permissions issue that prevented me from driving `make` directly.
