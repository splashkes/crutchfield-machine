# ADR-0016 — Sanitize NaN/Inf at output_fade, never anywhere else

**Status:** Accepted
**Date:** 2026-04-20
**Retroactive:** no

## Context

[ADR-0015](0015-pipeline-order-and-float-preservation.md) removed the
defensive `clamp(rgb, 0, 1)` from `physics` and `contrast` so the
float feedback buffer would actually carry HDR signal. That ADR's
"Consequences" section argued the `decay` layer was sufficient as a
dynamic-range pull-back. Testing showed that claim was too strong.

**The failure mode.** A user dials `uContrast` well above 1 for a few
frames, generating an S-curve overshoot. Values in some pixels cross
the finite-float boundary and become `+Inf`. Subsequent frames
propagate the Inf through multiplicative decay (`Inf * 0.99 = Inf`),
through gamma (`pow(Inf, 1/γ) = Inf`), and eventually through some
arithmetic that yields `NaN` (e.g. `Inf - Inf` in a mix, or `Inf * 0`
in coupling). Once a pixel is `NaN`, **nothing in the pipeline can
recover it**:

- `decay_apply`: `NaN * uDecay = NaN`.
- `couple_apply` / `external_apply`: `mix(NaN, x, a) = NaN*(1-a) +
  x*a`, and `NaN * (1-a) = NaN` even at `a=1` because IEEE-754 says
  `NaN * 0 = NaN` (not 0).
- `inject_apply` (same story): `mix(NaN, pattern, uInject) = NaN`
  even when `uInject = 1.0`. This was the observed symptom — "inject
  doesn't work any more" after a divergent run.
- `noise_apply`: `NaN + anything = NaN`.

The field stays poisoned across frames. The only existing recoveries
are `C` (clear fields — resets `uPrev` to zero) and restarting. Both
require the user to know what happened.

## Decision

Add a **single** per-component NaN/Inf sanitize inside
`output_fade_apply`, at the very end, replacing any non-finite
component with `0.0`:

```glsl
vec3 sanitize_finite(vec3 v) {
    // Ternary (not mix()) because mix(NaN, 0, 1) = NaN*0 + 0*1 = NaN.
    return vec3(isnan(v.r) || isinf(v.r) ? 0.0 : v.r,
                isnan(v.g) || isinf(v.g) ? 0.0 : v.g,
                isnan(v.b) || isinf(v.b) ? 0.0 : v.b);
}
```

This is the **only** point in the default pipeline where we
conditionally rewrite a colour component for non-range reasons.

**Why `output_fade` specifically.** It is unconditionally last in
`main.frag`'s dispatch, runs without an enable gate, and its
semantic is already "the value committed to the feedback field."
Attaching the sanitize to it means *every* path — regardless of
which layers are toggled — writes finite values.

**Why `mix()` is not used.** IEEE-754 requires `NaN * 0.0 = NaN`, so
`mix(NaN, 0.0, 1.0) = NaN*(1.0 - 1.0) + 0.0*1.0 = NaN`. A
ternary-select on each component is the only reliable way to
substitute the poisoned value.

**Why per-component, not per-pixel.** If one channel has diverged but
the other two haven't, preserve the healthy channels. Per-pixel
(`if (any(isnan(col.rgb))) col.rgb = vec3(0);`) would throw away
information unnecessarily and produce visible black pixels instead of
colour shifts.

**Why replace with 0 and not some other value.** Zero is the identity
for additive ops (`couple`, `external`, `noise`, `inject`) and the
annihilator for multiplicative ops (`decay`). Either behaviour is
acceptable after divergence — the pixel effectively restarts from
black on subsequent frames, matching what the `C` key does globally.

## Consequences

### Wins

- Divergent dynamics self-heal over a few frames instead of requiring
  manual recovery. User can push contrast hard, generate overshoot,
  and the system finds its way back.
- Injection remains reliably responsive regardless of prior field
  state. The reported "inject stopped working" class of bug goes
  away.
- Recorders and screenshots never capture `NaN` pixels — which would
  be a real problem for EXR consumers that assume finite values.
- The full HDR range is still preserved. Only non-finite components
  are touched; any finite value, including very large ones, passes
  through untouched.

### Costs

- One extra per-component compare-and-select per pixel per frame.
  Negligible on a 3090 (a few microseconds at 4K).
- A user pushing contrast into divergence now sees a *recovery
  transient* (brief black flashes in previously-Inf pixels) rather
  than a permanent stuck state. Arguably prettier; arguably less
  informative. Documented so nobody mistakes it for a bug.
- We now have one layer that the rest of the pipeline implicitly
  relies on for correctness. `output_fade` can't be disabled — good
  thing it never had an enable bit. Worth noting in LAYERS.md so
  anyone refactoring the post-stage is aware.

### Non-consequences

- Clamped-range output (`--precision 8`) is unaffected — unorm never
  produces NaN/Inf.
- EXR recording format is unchanged (EXR supports NaN/Inf, but we
  now never generate them at capture time).
- No change to the `C` key's clear-fields behaviour. That still
  does the global reset. The sanitize only catches *per-pixel*
  divergence without requiring global action.

## Alternatives considered

### Clamp everything to a large finite range (e.g. `[-1e30, 1e30]`)

Would bound runaway while still allowing overshoot. Rejected: `1e30`
is still a magic number, and arithmetic on very large finite values
can *still* produce `Inf` at the next frame (`1e30 * 1e30 = Inf` in
float32). The fundamental problem is that IEEE-754 has an absorbing
state; the only reliable fix is explicit detection.

### Sanitize inside `decay_apply`

`decay_apply` is the natural "pull back toward the attractor" stage,
so philosophically it looks like a fit. Rejected because:
- `decay` can be disabled (`F6`), at which point poisoning survives.
- Operating on `decay_apply`'s input means the next four layers
  (couple, external, inject, noise) could still introduce `NaN` of
  their own — though in practice they won't unless their inputs
  carry it.
- Placing the guard at the output makes the invariant "the feedback
  buffer contains finite values" trivially true, which is a cleaner
  statement than "the feedback buffer contains finite values
  *provided* decay is on."

### Sanitize in the blit pass (display only)

Would hide the issue on screen without healing the feedback buffer.
Rejected: exactly the wrong direction — the user would see a clean
image while the field stays poisoned, making the bug harder to
recognise and impossible to recover from without `C`.

### Re-introduce a range clamp somewhere

This would undo the entire point of ADR-0015. Rejected.

### Do nothing; teach users to press `C`

Rejected. Discoverability is terrible (no UI signal that the field
is non-finite), the workflow interruption is jarring, and recorders
would silently capture `NaN` frames in the interim.

## References

- [ADR-0001 — RGBA32F feedback buffer as default precision](0001-rgba32f-default-precision.md)
- [ADR-0015 — Preserve float headroom through the loop](0015-pipeline-order-and-float-preservation.md) — the decision this refines.
- [LAYERS.md §float-precision invariant](../LAYERS.md#float-precision-invariant) — updated to call out the sanitize.
- `shaders/layers/output_fade.glsl` — implementation.
- IEEE-754 2008, §6.2 — NaN propagation semantics (`NaN op x = NaN`,
  including `NaN * 0 = NaN`).
