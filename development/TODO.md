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

### ~~[P1] Non-empty default startup state~~ — DONE 2026-04-20

Landed: boot-inject picks a random inject pattern and fires at
`inject = 1.0` once after preset load, regardless of launch path.
Normal fade (`inject *= 0.85`) takes it out over ~20 frames.

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

### [P2] Audio → feedback coupling, signal-processing half (RMS / bands)

**Why:** The trigger-classification half of this (ADR-0018) landed
2026-04-20 — kick/snare/hat/bass/other envelopes now drive noise-
dropout glitch flavours. But the classifier only reacts to
`Audio::trigger()` calls from our internal engine; live mic input,
DAW output, ambient sound, anything NOT fired by our voice pool is
invisible. The signal-processing complement (RMS + 3-band split on
the master bus) would restore general audio-reactivity.
**Where:** extend `audio.cpp` with an analysis ring on the master
bus. Expose via `Audio::levels()` returning a POD consumable from
`main.cpp`. Publish as additional uniforms alongside the existing
`uMus*` triggers. The two systems coexist cleanly.
**Done when:**
- `Audio::levels()` returns `{rms, bassRms, midRms, highRms, onset}`.
- At least one visual consumer uses it (e.g. midRms driving a
  continuous blur modulation, bassRms driving zoom).
- Example preset (`music/06_selfmodulated.strudel` plus visual
  tweaks) demonstrates the combined loop.
**Effort:** medium.
**Reference:** ADR-0018 for the complementary trigger bridge.

### [P2] Euclidean rhythms in the pattern engine

**Why:** `.euclid(3, 8)` is one of Strudel's most-used combinators.
Our `_UNIMPL_METHODS` safety net currently no-ops it, which means
pasted snippets that depend on it produce silence instead of the
expected drums. Low effort, high compat payoff.
**Where:** `js/engine.js`. Implement as a Pattern method that returns
a new Pattern whose query generates N hits spread Euclideanly across
the cycle's M steps.
**Done when:**
- `s("bd").euclid(3, 8)` and `s("bd").euclid(5, 8)` produce the
  expected Bjorklund distributions.
- `.euclidLegato(...)` and `.euclidRot(...)` also land (common
  variants).
- Removed from `_UNIMPL_METHODS`.
**Effort:** small.
**Reference:** ADR-0012 (what we didn't ship from Strudel).

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

### [P2] Package the macOS build as a real `.app`

**Why:** The native Apple Silicon build now works from the repo root,
but camera permission UX is still bare-binary territory. A proper `.app`
bundle improves launch ergonomics, makes privacy settings more legible,
and is the first step toward codesigning/notarization.
**Where:** new macOS packaging path; likely `Makefile.macos`, an app
bundle skeleton, and `development/apple_silicon.md`.
**Done when:**
- `make -f Makefile.macos dist` can optionally produce an `.app`
  bundle or a zip containing one.
- Camera permission prompts are stable and documented against the app
  bundle, not just the launching terminal.
- `Info.plist` includes the camera usage description.
**Effort:** medium.

---

## P3 — backlog

### [P3] GPU-side scalars published to `fb.*`

**Why:** Current `fb.*` scalars come from `Params` (user-set values).
Real visual state — mean luminance, dominant hue, motion energy,
histogram peaks — lives in the shader textures. Getting them into JS
means reading back from the GPU. Mipmap-based approach (1×1 mip = mean)
is cheap but adds a pipeline hop. Worth the latency tradeoff?
**Where:** `main.cpp` render loop after blit; extra mipmap generate on
the sim FBO, read 1×1 level via `glGetTexImage` into a CPU buffer, call
`Music::setScalar("meanL", …)`.
**Done when:**
- At least `fb.meanR/G/B/L`, `fb.motionEnergy` exposed to JS patterns.
- Example preset demonstrates dominant-hue → chord color mapping.
- Measured cost < 0.5 ms/frame at 4K.
**Effort:** medium.

### [P3] Value-transforming pattern combinators (`.range`, `.add`, `.sub`)

**Why:** Intentionally left as crash rather than no-op (ADR-0012) —
users get an explicit error instead of silently wrong output. Proper
implementation would let patterns do things like `sine.range(200,
1000).lpf(pat)` which Strudel users expect.
**Where:** `js/engine.js`. Requires value-pattern support — the pattern
itself produces numeric values at query time, consumed by a setter
that accepts either a Pattern or a scalar.
**Effort:** medium (requires rethinking the value model).

### [P3] Master gain / soft limiter

**Why:** Current audio has no headroom protection. Triggering lots of
simultaneous synth voices can clip audibly. A cheap soft-knee limiter
on the master bus prevents this without colour.
**Where:** `audio.cpp` after the dry/delay/reverb sum, before the
device write.
**Effort:** small.

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

### [P2] Spout GPU-texture-share output for live OBS at float precision

**Why:** OBS window-capture goes through the 8-bit sRGB display
framebuffer, losing the float headroom the sim runs at. For live
streaming / real-time monitoring at full quality, Spout (Windows
DX/GL shared texture) is the standard — OBS has a Spout source
plugin, and our RGBA16F sim FBO could be published as a named
texture. Alternative: NDI (lower precision, cross-machine capable).
**Where:** new module `spout.cpp/h`, hook into the render loop
after the blit. CLI flag `--spout [name]` to enable. Spout SDK
is MIT with OpenGL bindings.
**Done when:**
- feedback.exe can publish its sim FBO via Spout under a
  user-chosen sender name.
- OBS Spout source picks it up and renders without 8-bit flattening.
- Documented in README next to the EXR recorder as the "live
  high-fidelity out" path.
**Effort:** medium (~4-6 hours including SDK integration + test).

### [P2] Retune curated presets for new default enable state

**Why:** `01_default.ini` through `05_kaneko_cml.ini` were all saved
before the 2026-04-20 extended session flipped `enable` default from
a 7-layer mask to `L_ALL`, and before the physics/contrast clamp
removals. Their visual character shifted — louder colour, different
fade rate, thermal shimmer now on by default. Each preset needs
an aesthetic calibration pass.
**Where:** `presets/0[1-5]_*.ini`. Playable content only; no code.
**Done when:**
- Each curated preset re-saved after a dial-in session that
  respects its original intent.
- Preset headers (the `# notes:` lines) updated to describe the
  new post-ADR-0015 / ADR-0017 character.
**Effort:** small (one session of dial-in).

---

## Recently completed

*Remove from this list after ~2 releases.*

**Landed 2026-04-20 (post v0.1.3 working branch — performance-layer expansion):**

- Pipeline reorder: decay before mixers, inject before noise (ADR-0015).
- Removed [0,1] clamps from physics.glsl + contrast.glsl (ADR-0015).
- NaN/Inf sanitize at output_fade (ADR-0016).
- New canonical doc `development/LAYERS.md`.
- Pixelate layer as alternate sample stage — 9 styles × 6 bleed
  presets, cell-size scaling by sim resolution, warp+thermal
  re-applied to cell centres so pixelation propagates (ADR-0017).
- Burn bleed preset with reseed action (Alt+Delete) for
  dead-pixel-group simulation.
- Noise archetypes extended 2 → 5: white, pink 1/f, heavy static,
  VCR rolling bar, dropout.
- Inject patterns extended 5 → 10: noise field, concentric rings,
  spiral, polka dots, starburst (keys `6-0`). Plus pattern 10
  "bouncer" (animated, 10-second hold) on Alt+B.
- Pattern alpha channel: `pattern_gen()` now returns `vec4` so
  sparse / animated patterns don't wipe the feedback field.
- Boot-time random inject so startup is never black-screen.
- Music → visual trigger bridge (ADR-0018). Kick/snare/hat/bass/
  other event-level classification in `audio.cpp`; per-bucket
  decaying envelopes drive distinct dropout glitch flavours.
- Beat-driven hue jump modulation (Ctrl+Alt+H toggle,
  Ctrl+Alt+=/- step nudge, 0-100 range). On by default at step 12
  for a gentle colour cycle.
- Beat-driven invert flip modulation (Ctrl+Alt+V toggle,
  Ctrl+Alt+,/. divisor nudge). Flips `p.invert` every Nth beat.
- Invert frame-divider fix (`p.invertPeriod` default 20) — turned
  a 60 Hz strobe into a slow 3 Hz flip.
- Brightness knob (Alt+Up/Down) applied in `blit.frag` — display-
  only, doesn't feed back, doesn't affect EXR recording.
- Default `enable = L_ALL`; all 12 layers on at startup.
- Default `bpmInject = false`, `bpmHueJump = true` at step 12.
- Pause couples to music (remembers pre-pause music state).
- Exit-confirm modal (first Esc → "Really quit? Y/N"; second Esc
  or Y confirms, N cancels).
- Layer-off warnings — nudging a parameter whose layer is off posts
  a HUD hint (e.g. "contrast set — layer off, F5 to activate").
- Help panel redesign: removed the full-panel opaque dim; per-line
  dark backings via `drawTextBacked` so feedback shows through
  between rows.

**Landed in PR #6 (`music`) — merged 2026-04-20, released as v0.1.3:**

- Strudel-compatible native music engine inside feedback.exe: embedded
  QuickJS + clean-room pattern DSL + miniaudio backend + 24-voice
  pool with sampler, synth (sine/saw/square/tri + ADSR), biquad LPF/
  HPF, delay bus, Freeverb-style reverb.
- `music/*.strudel` preset system with hot-reload (~250 ms after save).
  5 shipped presets: breakbeat, climb, dub_pulse, acid, pad_drift.
- Virtual MIDI port `feedback` via teVirtualMIDI driver (ADR-0011).
  First-run experience: Ctrl+M or picker option #7 auto-installs
  loopMIDI via winget, port appears silently.
- Strudel → feedback MIDI Clock sync drives BPM when live (24 PPQN
  rolling average), Note-On fires mapped actions, CC drives
  continuous params via existing binding syntax.
- Video→JS scalar bridge: 12 `fb.*` globals readable from patterns
  (`fb.zoom`, `fb.theta`, `fb.decay`, etc.) so visuals modulate
  music in real time.
- Live gesture: holding Space jumps to the breakbeat preset while
  held and restores on release (combined with the existing
  inject-pattern visual trigger).
- New Music help section (ADR-0013 locked in the dt-coupled scheduler).
- ADRs: 0010 (QuickJS), 0011 (virtual MIDI), 0012 (clean-room pattern
  engine), 0013 (dt-coupled scheduler).

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
