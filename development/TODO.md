# TODO — prioritized backlog

Living document. Pick an item, mark it in-progress, complete it, delete
the entry. If you discover work during a task, add it here.

## Item format

```markdown
### [priority] Title in imperative form

**Why:** the motivation. If you can't explain why, prune the item.
**Where:** specific files and (if known) line numbers or symbols.
**Done when:** acceptance criteria — how you know the task is finished.
**Effort:** tiny (<30min) / small (<2hr) / medium (<1day) / large (multi-day).
**Blockers:** anything that must happen first.
```

Priorities:
- **P0** — blocker; do first.
- **P1** — important; next sprint.
- **P2** — valuable; no rush.
- **P3** — backlog; consider if nothing more urgent.

---

## P0 — blockers / embarrassments

### [P0] Fix EXR recorder exit-hang at 4K60

**Why:** On exit with 100+ frames in the RAM buffer, `stop()`
synchronously joins encoder threads while they drain to disk. The
window goes "Not Responding" for up to ~30s. To users this looks like
a crash — the main thing that'll make the v0.1.0 release feel broken.
**Where:** `recorder.cpp: stop()`; main loop exit path in
`main.cpp` around line 2000 where `S.rec.stop()` is called.
**Done when:**
- Window stays responsive during drain (events pumping).
- An overlay/HUD element shows drain progress ("Saving: N frames
  remaining").
- Exit completes cleanly once encoders finish, not mid-stream.
- User no longer tempted to force-kill.
**Effort:** medium. Scoped plan: split `stop()` into `beginStop()`
(quick — signals & joins readers) and `finishStop()` (joins encoders
& cleans up). Add `pendingEncodes()` query. Main-loop drain page shows
overlay with a frame count until pending is 0. See ADR-0005
"Consequences" section for more context.

### [P0] Automated smoke test

**Why:** Every release step is currently manual. A regression that
breaks shader compilation, preset loading, or basic rendering would
get shipped. Need at least a headless "launch, render one frame, exit
cleanly" test that runs in CI and on a developer's machine before
release.
**Where:** new `tests/` directory. Could shell out to the binary with
a flag like `--self-test` that renders one frame and exits 0 on
success.
**Done when:**
- `make test` passes on a fresh build.
- Test covers: startup, shader build, one render iteration, preset
  load (e.g. `01_default.ini`), clean exit.
- Test is wired into a GitHub Action that runs on push to main.
**Effort:** medium.

---

## P1 — important

### [P1] GitHub Actions CI for Windows builds

**Why:** Every contributor currently needs to set up MSYS2 locally
before their first PR. A CI job catches "works on my machine" issues
and produces release zips automatically on tag.
**Where:** new `.github/workflows/build.yml`. Use the existing
Makefile; match the MSYS2 MINGW64 environment.
**Done when:**
- PR builds get a green check verifying `make` succeeds.
- Tag pushes produce `make dist` artifact uploaded to the Release.
- DLL import-table check runs automatically and fails the build if
  an unexpected DLL appears.
**Effort:** small.

### [P1] Pre-built screenshot / demo GIF in README

**Why:** Current README has four stills (good), but a short animated
clip would communicate what the *dynamics* look like in a way stills
can't. Primary conversion moment for visitors.
**Where:** `gallery/demo.gif` or `.mp4`, referenced in `README.md`
near the existing 2×2 grid.
**Done when:**
- ~10-15 second clip showing visible motion from a curated preset.
- Under 5 MB so GitHub renders it inline.
- Linked from README above the fold.
**Effort:** small. Record with OBS, re-encode to GIF or MP4.

### [P1] Non-empty default startup state

**Why:** A new user who double-clicks the exe, picks "Default" in the
picker, sees a black screen and has to know to press `3` + `Space`.
Losing this audience. Option 1 should load `01_default.ini` and
auto-inject once on startup so something is visibly alive.
**Where:** `main.cpp: main()`, right after preset_rescan. Trigger
`S.p.inject = 1.0f` once after the preset loads if auto-launched
from the picker.
**Done when:**
- Picker option 1 or a no-args launch produces visible motion within
  2 seconds of window appearing.
- Doesn't affect CLI users with explicit `--preset` or `--demo`.
**Effort:** tiny.

### [P1] Shader hot-reload error visibility in-window

**Why:** `\` reloads shaders. If compile fails, the error goes to
stderr — invisible to fullscreen users and non-console users. They see
"reload didn't work" with no explanation.
**Where:** `main.cpp: reload_shaders()`; `overlay.h` needs a way to
show multi-line error text.
**Done when:**
- GLSL compile errors render as a toast or small panel in the window.
- Successful reload still shows the "shaders reloaded" toast.
**Effort:** small.

### [P1] `--display N` flag for targeting a specific monitor

**Why:** Matters for the Elgato / OBS capture workflow. Users with a
capture card loop currently have to drag the window manually.
**Where:** `main.cpp: parse_cli()`, `Cfg`, window creation branch for
`--fullscreen`. `glfwGetMonitors()` enumerates; pass the Nth one to
`glfwCreateWindow`.
**Done when:**
- `--display N` selects the Nth connected monitor for `--fullscreen`.
- `feedback --help` lists available displays if user passes an
  invalid index.
**Effort:** small.

---

## P2 — valuable, no rush

### [P2] Audio reactivity (WASAPI loopback)

**Why:** Feedback systems pair naturally with sound. An RMS envelope
from system audio driving `uZoom`, `uHueRate`, or `uInject` would
open up live-performance use cases and create gorgeous music-video
output.
**Where:** new `audio.cpp/h` (WASAPI loopback client). Expose a
uniform like `uAudioLevel` in main.frag; let users bind which param
it modulates via a CLI flag or preset key.
**Done when:**
- Audio level readable from any shader layer as a `float` uniform in [0,1].
- Flag to enable: `--audio-reactive zoom` or similar.
- Works on Windows via WASAPI; Linux/macOS paths deferred.
**Effort:** medium.

### [P1] Real MIDI input (for Strudel sync + hardware controllers)

**Why:** The action-registry refactor (ADR-0007) already put a
`[midi]` section in `bindings.ini` and a stub `Input::pollMidi`.
Strudel integration needs real MIDI-in to follow its clock and fire
visual events on drum hits. Same plumbing unlocks physical MIDI
controllers (Launchpad, nanoKONTROL) at no extra cost.
**Where:** `input.cpp::pollMidi`. Use winmm (`midiInOpen`,
`midiInStart`) — already linked for the recorder; no new dependency.
Thread-safe queue: callback pushes bytes, main thread drains.
**Done when:**
- MIDI Clock (0xF8) at 24 PPQN derives BPM; Start/Stop anchors phase.
- Note-On events bind to any ActionId via `bindings.ini`
  (`bpm.flash = note:36 ch=10` etc.).
- CC events drive continuous parameters via the existing axis/rate
  dispatch path.
- A new `MIDI` help section shows port status, derived BPM, last
  note, last CC.
- Works alongside loopMIDI (Windows virtual MIDI) so a Strudel
  browser session can drive the feedback app.
**Effort:** medium.
**Reference:** full plan in `development/plans/strudel_midi_sync.md`.
**Blockers:** PR #1 (`roland-v4` branch) merged into main first.

### [P2] `--record-on-start`

**Why:** Unattended-capture workflow for gallery installations —
launch with `--demo --record-on-start`, walk away, come back in 8
hours with EXR sequences on disk.
**Where:** `main.cpp: parse_cli()`, Recorder activation near startup.
**Done when:**
- Flag starts the recorder on the first frame.
- Combined with `--demo` produces a multi-hour run without human input.
- Graceful handling of disk-full conditions (pause recording, print
  warning, resume if space frees up).
**Effort:** small core + some polish for the disk-full case.

### [P2] In-window preset menu overlay

**Why:** Non-CLI users currently have to quit and re-launch the app
to switch to a preset they haven't assigned to `Ctrl+N/P` order.
A keyboard-navigable overlay lets them browse live.
**Where:** new functionality in `overlay.cpp/h`; hotkey in
`main.cpp: key_cb`.
**Done when:**
- Hotkey (e.g. `Tab`) toggles a preset browser overlay showing all
  `presets/*.ini` names.
- Arrow keys navigate, Enter loads, Escape closes.
- Works in fullscreen.
**Effort:** medium. **Note:** another contributor may be tackling this
in a separate worktree — coordinate before starting.

### [P2] Unify layer-bit enum between host and GLSL

**Why:** `L_*` bits are defined twice — once in main.cpp, once in
main.frag — and must match by hand. Silent breakage when they drift.
**Where:** could be solved by a tiny `layers.inc` header generated
by a script, or a pre-compile step that reads main.frag and emits the
enum.
**Done when:**
- Adding a layer bit in one place automatically updates the other.
- Build fails (or warns) if they drift.
**Effort:** small-medium. Low immediate priority because rarely
changed, but valuable long-term.

### [P2] Cross-platform smoke of Linux / macOS ports

**Why:** The `linux/` and `macOS/` subdirs exist but are untested
against current shaders. At minimum, document what state each is in.
**Where:** `linux/README.md`, `macOS/README.md` — already annotated as
WIP, but no one has confirmed they even compile against the latest
shaders.
**Done when:**
- Build status documented for each platform (does main.cpp compile?
  do shaders compile? does the app launch?).
- Known-broken bits listed explicitly.
**Effort:** small per platform, medium in total.

---

## P3 — backlog

### [P3] Preset browser + search

Extension of the in-window preset menu: filter by typing, preview on
hover. Depends on P2 item above landing first.

### [P3] `.lnk` shortcut launchers in dist zip

Redundant now that the in-app picker exists, but could still be a
nice touch for Start menu integration. Low priority.

### [P3] Replace `build_msvc.bat` with CMake

Would let MSVC users build the project without the hand-written batch
script, and would enable Visual Studio project generation. `make dist`
would remain the release path.

### [P3] Unified preset format (YAML? TOML? stay INI?)

INI is simple and works, but doesn't nest well. If we add per-layer
parameter groups or multi-stage chains, TOML or YAML would be cleaner.
Not worth churning the ecosystem for now.

### [P3] Tutorial / "first 10 minutes" video

Short onboarding video demonstrating launch → pick preset → tweak →
save → record. Would help onboarding the "target audience #1"
(installation artists).

### [P3] Preset trading / sharing infrastructure

A GitHub Discussions category for preset sharing, or a curated
`community-presets/` folder. Only worth doing once we have a community
to share with.

---

## New for P1/P2 — follow-on work from `roland-v4` branch

### [P2] A/B bus V-4 architecture refactor

**Why:** The `roland-v4` pass shipped V-4 effects as single-bus
(ADR-0008) with a documented holdback list — 216 wipes, 36-entry
Transformer matrix, 8-slot Memory, Preview bus, Presentation mode,
V-LINK — none of which can be ported without an A/B bus. A faithful
V-4 port eventually wants that foundation.
**Where:** `Params` splits into `BusParams` × 2 + a top-level `Master`.
`render_field` duplicates per-bus. New `combine.frag` reads both
buses + `uTbar` and outputs the program frame. Actions prefix with
`a.*` / `b.*` or introduce an "active bus" focus state.
**Done when:**
- Both buses run independent pipelines at full 4K60.
- T-bar crossfade between them, with selectable curve (A/B/C per
  V-4 OM §17).
- At least one wipe and one key transition working on the crossfade.
- Preset format migrates; old single-bus presets load into bus A
  with bus B defaulted.
**Effort:** large (multi-day).
**Reference:** `research/edirol_v4_ab_bus_future.md` has the
implementation sketch.

### [P2] Persist active help section across runs

**Why:** Overlay tracks `activeSection` (which section the gamepad
drives when help is closed) but resets to Warp on every launch.
Cheap to persist.
**Where:** `bindings.ini` top-level `activeSection = <name>` key;
write on change, read at startup.
**Done when:** Last-used section is restored next launch.
**Effort:** tiny.

### [P2] `presets/README.md` describing the curated presets

**Why:** `01_default` through `05_kaneko_cml` ship with the release
but only have one-line `# notes:` headers. A one-paragraph description
per preset turns them from mystery files into a guided tour. Especially
needed now that the help panel surfaces presets but has no meta.
**Where:** new `presets/README.md`.
**Done when:**
- Each of `01_*` through `05_*` has a 2-4 sentence description of
  what it does, what makes it distinct, and what parameters to tweak
  from there.
- Linked from root README's `CLI flag cookbook > Presets` section.
**Effort:** small.

---

## Recently completed

*Remove from this list after ~2 releases.*

**Landed in PR #1 (`roland-v4`) — merged 2026-04-20:**

- Action registry + `bindings.ini` for unified keyboard / gamepad /
  MIDI dispatch (ADR-0007).
- Xbox gamepad support with contextual per-section mapping (ADR-0009).
- Help overlay rework — top-left drill-down panel, 16 sections with
  live values and per-section gamepad legends.
- Edirol V-4-inspired effect slots: 2 slots × 18 effects + output
  fade (ADR-0008).
- BPM sync + tap tempo + 5 beat-locked modulations (inject-on-beat,
  strobe-rate lock, vfx auto-cycle, fade flash, decay dip).
- Cursor-nav pattern for Layers / Quality / Inject list sections.
- Luminance-invert fix: lifted out of L_PHYSICS gate so it always
  works.
- `build_msvc.bat` missing sources (recorder, overlay, input) fixed.
- Gamepad help discoverability: Back button = help, bottom-right
  section tag when help is closed.
- Bindings parser/writer learned `PrtSc` round-trip and `<NNN>`
  numeric fallback (fix for first-run regression).

**Landed in v0.1.0:**

- Static-link Windows binary for DLL-free distribution (ADR-0003).
- `make dist` target.
- `--precision 8` RGBA8 unorm feedback mode.
- `--preset NAME` startup load.
- `PrtSc` screenshot.
- Auto-demo mode (`--demo`).
- Demo-mode inject-on-preset-cycle fix.
- Console mode picker on `argc == 1` (ADR-0006).
- CONTRIBUTING.md with shader authoring walkthrough.
- Release gallery images via orphan branch (no asset list clutter).
- `development/` canonical docs.
