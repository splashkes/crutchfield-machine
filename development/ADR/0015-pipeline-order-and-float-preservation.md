# ADR-0015 — Preserve float headroom through the loop, and reorder the feedback-write tail

**Status:** Accepted
**Date:** 2026-04-20
**Retroactive:** no

## Context

A review of the shader pipeline (`shaders/main.frag` + `shaders/layers/*.glsl`)
surfaced two classes of issue:

### 1. Silent [0,1] clamps undoing the float-precision invariant

ADR-0001 commits the project to running the whole feedback loop in
RGBA32F (or RGBA16F with `--precision 16`), on the stated principle
that "we don't clamp to 8-bit anywhere in the default feedback path"
([DESIGN.md §Precision over prettification](../DESIGN.md#precision-over-prettification)).

Two layers were violating this:

- `physics.glsl`'s `physics_apply()` ended with
  `return vec4(clamp(rgb, 0.0, 1.0), c.a);`
- `contrast.glsl`'s `contrast_apply()` ended with
  `return vec4(clamp((c.rgb - 0.5) * uContrast + 0.5, 0.0, 1.0), c.a);`

Both clamps were defensive, not semantic. Their effect was to silently
compress any high-dynamic-range shaping (S-curve overshoot, Reinhard
soft-knee pre-rolloff) into the display range at an *intermediate*
stage of the loop — i.e. the exact failure the float buffer exists to
prevent. On the default preset with `uContrast > 1`, the loss was
observable: the attractor reads as "8-bit-y" where the math says it
should have HDR highlights.

Note that `gamma.glsl`'s `max(c.rgb, vec3(0.0))` is not a clamp of the
same kind — it defends `pow()` against negative inputs producing NaN.
That's correctness, not range compression.

### 2. Feedback-write tail was out of physical order

The original order was:

```
… gamma_out → couple → external → decay → noise → inject → vfx → output_fade
```

This had two issues:

- **`decay` running after the mixers** meant that freshly-mixed camera
  (`external`) and partner-field (`couple`) content was immediately
  attenuated by `uDecay` on the very frame it entered. That contradicts
  the analog-rig metaphor the rest of the pipeline is built around — a
  camera pointed at a CRT sees live scene content at full brightness,
  not at the CRT's attenuated brightness.
- **`inject` running after `noise`** meant that triggered patterns
  (H-bars, dot, checker) landed on top of noise rather than being
  textured by it. The result is that injected perturbations read as
  visibly synthetic against the noisy feedback they perturb — a tell
  that doesn't match how a real signal-injection artifact would look.

Both of these are aesthetic choices, but the current ones were not
deliberate — they were what the pipeline had grown into.

## Decision

**Float preservation.** Both offending clamps are removed.
`physics_apply` returns its processed `rgb` unclamped; camera-side
saturation remains available through `uSatKnee` (Reinhard rolloff)
which is the physically meaningful model. `contrast_apply` returns its
S-curve output unclamped; runaway values are the job of `decay` to
pull back.

**Pipeline reorder.** The feedback-write tail becomes:

```
… gamma_out → decay → couple → external → inject → noise → vfx → output_fade
```

Semantics in the new order:

1. `decay` attenuates the loop signal first, before any external
   content is mixed in.
2. `couple` then `external` add partner-field and camera content at
   full amplitude.
3. `inject` overrides with a triggered pattern when held.
4. `noise` is the final additive stage before post — matching the
   physical intuition that thermal noise is the last thing added to
   any real signal before it's written.

Post (`vfx_slot[0]`, `vfx_slot[1]`, `output_fade`) is unchanged.

## Consequences

### Wins

- The float buffer is no longer silently truncated mid-loop.
  High-contrast presets get the HDR headroom they were paying for.
- Camera and partner-field mixing produce the expected "fresh input
  on faded feedback" look, instead of "everything decays together."
- Injected patterns integrate with the noise floor instead of
  sitting on top of it.
- The pipeline is now internally consistent with the analog-rig
  metaphor stated in DESIGN.md.

### Costs

- **Every curated preset (`presets/01_*.ini` through `05_*.ini`)
  was hand-tuned against the old pipeline** and will look different
  after this change. A retuning pass is required; it is tracked as
  a follow-up and is not part of this ADR.
- Users on the old build with saved auto-presets
  (`presets/auto_*.ini`) will see their saves behave differently.
  The preset schema itself is compatible — only the visual
  interpretation shifts.
- A layer that needed the [0,1] guarantee of the old clamp (none
  currently do) would now have to assert that locally.

### Non-consequences

- No change to the host state machine or `Params` struct.
- No change to preset file format.
- No change to recording format (the EXR recorder already captured
  from the float FBO, so it never saw the clamped values anyway —
  meaning old recordings of the same preset will look *more like the
  EXR output* than they used to on screen).

## Alternatives considered

### Keep the clamps, fix the overshoot elsewhere

A `clamp(rgb, -K, K)` with `K = 8.0` or similar would have kept some
of the defensive intent while preserving headroom. Rejected: it's a
magic number with no physical meaning, and the natural bound is
whatever the dynamics settle to, not a hardcoded cap.

### Add a single post-pipeline clamp at `output_fade` or `blit.frag`

Rejected: the blit pass already does an implicit display-range mapping
via the default framebuffer's 8-bit / sRGB conversion, and the
recorder captures from the sim FBO *before* blit, so it sees the full
float signal. Adding a clamp somewhere just to be safe re-introduces
the exact problem this ADR removes.

### Keep the old order, treat the mixer-then-decay behaviour as intentional

Rejected: it was not intentional; it was legacy. Documenting an
accidental ordering as a deliberate design choice is how codebases
accumulate cargo-cult.

### A comprehensive reorder (not just the tail)

Considered and scoped out. The UV-space → sample → camera-side →
analog → feedback-write → post groupings are themselves defensible
and move as a unit. Within-group reorder is a separate set of soft
aesthetic choices (see [LAYERS.md](../LAYERS.md) for the full
constraint map); changing them without aesthetic A/B evaluation would
be guessing.

## References

- [ADR-0001 — RGBA32F feedback buffer as default precision](0001-rgba32f-default-precision.md)
- [ADR-0004 — One `.glsl` file per layer](0004-shader-layer-system.md)
- [development/LAYERS.md](../LAYERS.md) — canonical pipeline-order reference, updated to reflect this ADR.
- [DESIGN.md §Precision over prettification](../DESIGN.md#precision-over-prettification)
- `shaders/main.frag` — dispatch order
- `shaders/layers/physics.glsl`, `shaders/layers/contrast.glsl` — clamps removed here
