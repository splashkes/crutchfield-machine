# ADR-0008 — Edirol V-4 as single-bus effect slots, not a full A/B mixer port

**Status:** Accepted
**Date:** 2026-04-19

## Context

The Edirol V-4 (Roland, 2003) is a standard-definition four-channel
video mixer with a rich effects catalogue: 96 effects across 18
families (Strobe, Still, Mirror, Multi, Colorize, keys, PinP, etc.),
248 transitions (1 Mix + 216 wipes + 4 keys + 27 slides), 8 memory
slots, BPM sync, two Transformer buttons with a 36-entry action
matrix, a Preview bus, Presentation mode, and V-LINK MIDI extension.

We wanted to port much of this character into the feedback system —
the V-4's creative vocabulary is close to what feedback art needs —
but the V-4 is fundamentally a **two-bus mixer with a T-bar
crossfade**. Our `Params` is single-bus. A faithful port would mean a
full architectural refactor: duplicating every layer's state onto
bus-A and bus-B, adding a combine stage, reworking every key binding
and help section to target a bus.

Research captured in `research/edirol_v4_feature_inventory.md`.

## Decision

Ship V-4 integration as a single-bus pass. Specifically:

**Included:**
- Two V-4 effect slots (`vfxSlot[2]`), back-to-back at the tail of
  the pipeline (after `inject_apply`). Each slot holds one of 18
  effects or is off.
- Collapsed effect families: the V-4 has 5–10 variants per family
  (STROBE1..10, MULTI-H1..5, etc.); we expose one effect per family
  with a continuous CONTROL parameter that sweeps through the
  variation space.
- **18 effects** — Strobe, Still, Shake, Negative, Colorize,
  Monochrome, Posterize, ColorPass, Mirror-H/V/HV, Multi-H/V/HV,
  W-LumiKey, B-LumiKey, ChromaKey, PinP.
- Per-slot **B-source** selector (camera | self-reprocessed previous
  frame) for key/PinP families that need a second source.
- **Output fade** — bipolar dial to black/white at the very end of
  the pipeline. Implements the V-4's Output Fade dial as a single
  control.
- **BPM sync + tap tempo** with five beat-locked modulations:
  inject-on-beat, strobe-rate lock, vfx auto-cycle, fade flash,
  decay dip.

**Explicitly held back** (documented in
`research/edirol_v4_ab_bus_future.md`):
- A/B bus architecture and T-bar crossfade.
- 216-wipe bank + 27 slide transitions + 4 key transitions.
- Transformer button matrix (kept only fade-to-W/B entries).
- 8-slot Memory ring (our preset files cover the use case).
- Preview bus.
- Presentation mode.
- V-LINK protocol layer.

## Consequences

**Positive:**
- Ports the most visually and rhythmically useful V-4 capabilities
  without blocking on a multi-week refactor.
- The two effect slots mirror the A-slot/B-slot gesture, so the
  mental model carries forward when we do eventually build A/B.
- BPM sync unlocks live-performance workflows; tap-tempo alone is
  a meaningful upgrade.
- Only two new shader files (`vfx_slot.glsl`, `output_fade.glsl`);
  fits cleanly into ADR-0004's layer system.

**Negative:**
- Chroma/Luma key and PinP effects are less expressive than on a
  real V-4 because there's no independent B-bus with its own
  processing chain. The "self-reprocessed" B-source partly
  compensates but isn't the same.
- Effects that depend on A/B (wipes, transformer, preview) can't
  be approximated meaningfully in single-bus; we don't ship them.
- The branch is explicitly named `roland-v4` but the result is
  "V-4-inspired," not V-4-faithful. The holdback doc makes that
  explicit so expectations are set.
- BPM modulations run off our internal beat clock, not synced to
  any external source yet. Strudel integration (planned; see
  `development/plans/strudel_midi_sync.md`) will connect an
  external master clock.

## Alternatives considered

- **Full A/B bus refactor.** Right architecture, wrong timeline.
  Would have taken weeks vs days. Captured as future work in the
  holdback doc so whoever picks it up has a running start.
- **Just ship the most useful effects without the V-4 framing.**
  We would have built the same 18 effects but without a
  CONTROL-parameter spine. The V-4 model gives us a coherent UX
  (slot + param + B-source) that scales if we later add more.
- **Port only the effects, skip BPM and output fade.** BPM is what
  makes the effect cycling come alive in performance. Separating
  them would have meant two PRs where one does the job.
- **Emulate the SD-era 8-bit pipeline** (4:2:2 chroma, NTSC
  sync). Interesting as a study but contradicts the precision
  principle (ADR-0001). Our pipeline stays float; the effects ride
  on top.

## References

- `research/edirol_v4_feature_inventory.md` — what we catalogued.
- `research/edirol_v4_ab_bus_future.md` — what we held back and why.
- `shaders/layers/vfx_slot.glsl` — 19-way dispatch (off + 18 effects).
- `shaders/layers/output_fade.glsl` — bipolar fade.
- `main.cpp` — `Params.vfxSlot[2]`, `vfxParam[2]`, `vfxBSource[2]`,
  `outFade`, BPM state.
- `main.cpp::update_bpm` — tap-tempo median + beat-event dispatch.
