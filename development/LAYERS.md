# LAYERS — shader pipeline reference

This is the canonical doc for the shader layer system: what each layer
does, in what order, and why. For *authoring* a new layer step-by-step
see [CONTRIBUTING.md](../CONTRIBUTING.md). For the overall system
architecture see [ARCHITECTURE.md](ARCHITECTURE.md). For *intent* see
[DESIGN.md](DESIGN.md).

## Pipeline order

```
UV-space          ┌── warp          (geometric transform of sample UV)
                  └── thermal        (heat-shimmer perturbation of sample UV)
                             │
sample            ┌── optics         (anisotropic blur + chromatic aberration)
                  └── pixelate       (grid quantization of the sample — takes over when style != 0)
                             │
camera-side      ┌── invert         (always-on; Crutchfield s parameter)
                  └── physics        (sensor gamma, saturation knee, RGB cross-couple)
                             │
analog stages    ┌── gamma_in       (linearise for linear-light math)
                  ├── color          (HSV hue-rotate + saturation)
                  ├── contrast       (nonlinear response curve)
                  └── gamma_out      (re-encode before mixing)
                             │
feedback write   ┌── decay          (per-frame attenuation of the loop signal)
                  ├── couple         (blend in partner field — Kaneko CML)
                  ├── external       (blend in live camera)
                  ├── inject         (pattern perturbation — triggered)
                  └── noise          (sensor floor; 5 archetypes white..dropout)
                             │
post             ┌── vfx_slot[0]    (V-4-style effect — dispatched by slot 0 id)
                  ├── vfx_slot[1]    (V-4-style effect — dispatched by slot 1 id)
                  └── output_fade    (bipolar fade to black / white — master)
```

The numbered order in `shaders/main.frag:main()` reflects this. Each
stage's call is one `if ((uEnable & L_X) != 0) col = X_apply(col, ...);`
line. Reordering is a matter of moving lines.

## Why this order

The pipeline mirrors the physical signal chain of an analog video feedback
rig: **camera optics → sensor response → analog processing → mixing with
other signals → sensor floor → triggered events → post effects → master
fade**.

Each grouping below corresponds to one "stage" of that chain and is
internally reorderable only with an eye on the consequences.

### UV-space stages — before the sample

`warp` and `thermal` modify the UV at which we read the previous frame.
They **must** precede `optics` (which performs the read). Their relative
order is a choice: warp applies a clean geometric map; thermal adds a
noisy perturbation on top. Thermal-after-warp means the shimmer rides
on the warped coordinate system, which feels right for "air between the
rig" — but thermal-before-warp is also defensible and would produce
shimmer that appears in untransformed image space.

### Sample — `optics` or `pixelate`

Optics is the normal read from `uPrev`. It fuses anisotropic blur and
chromatic aberration into one sampling operation because both are
spatial-kernel sums over texture reads. Splitting them would cost 2×
the samples for no benefit.

Pixelate **takes over** the sampling stage when `uPixelateStyle != 0`,
replacing optics (hard blocks do not want a blur kernel smearing
across cell edges). Each screen-space cell samples `uPrev` once at its
own `warp+thermal`-perturbed cell-centre position; dots / rounded
modes additionally fetch a per-pixel gap sample and mask between the
two. Because the cell-centre re-runs warp and thermal, pixelated
content propagates through the dynamics: as warp zooms the image,
next frame's cell reads the current frame's grid-warped equivalent,
which is exactly the "blocky attractor evolving through the loop"
behaviour we want. An earlier post-sample placement (which overwrote
the processed colour with a raw `texture()` read) broke this — the
pipeline's work downstream of pixelate was ignored by the cell
samples and the effect failed to propagate.

### Camera-side — `invert`, `physics`

These model the camera's response to incoming light. They run on the
colour returned by `optics`, before any "analog electronics" stages,
because they describe what the photoconductor does to the signal before
the electronics see it.

`invert` was lifted out of the `L_PHYSICS` enable gate so pressing "V"
works regardless of which other layers are on — see the comment at
`main.frag:110`. It's a dispatch-site toggle rather than an in-layer
branch.

`physics` layer intentionally does **not** clamp its output. The
Crutchfield camera-saturation model comes from the `uSatKnee` term
(soft Reinhard rolloff), not a hard clip. See the *float-precision
invariant* section below.

### Analog stages — `gamma_in`, `color`, `contrast`, `gamma_out`

Bracketed by gamma. Processing the loop in linear light changes the
dynamics meaningfully — blur and blends are not gamma-invariant, and
colour operations (hue rotation, contrast shaping) behave differently
in perceptual vs linear space.

`contrast` is deliberately unclamped: an S-curve pushed hard can drive
values outside [0,1], and losing that overshoot destroys high-dynamic-
range shaping downstream. The `decay` layer is the correct tool for
bringing values back down.

### Feedback write stages — `decay`, `couple`, `external`, `inject`, `noise`

This is the tail of the per-pixel map, and its order is the most
aesthetically load-bearing part of the pipeline. Current order:

1. **`decay` first.** The feedback signal attenuates before anything
   fresh is mixed in. This means the partner field (`couple`) and the
   camera (`external`) enter at full amplitude against a faded
   background — matching the analog rig where the camera sees live
   content at the brightness of the scene, not the attenuated
   brightness of the CRT it also happens to be pointing at.
2. **`couple` then `external`.** Both are `mix()` calls; order
   between them is aesthetic. Couple first means camera input rides
   on top of the coupled result, not vice versa.
3. **`inject` before `noise`.** Injected patterns (H-bars, dot, etc.)
   pick up the sensor-floor noise that follows them, rather than being
   pristine. This keeps triggered perturbations from reading as
   "synthetic" against the noisy feedback they land into.
4. **`noise` last** (before post). Matches physical sensor behaviour:
   thermal noise is the final thing added to the signal before the
   frame is committed. Five archetypes cycle via `Home`: white, pink
   1/f, heavy static, VCR, dropout. Amplitude (`uNoise`) scales them
   all so archetype × intensity work as two independent axes.
Pixelate lives at the sampling stage (see *Sample* section above),
not in the feedback-write tail. Cycled via `Delete`.

### Post — `vfx_slot[0]`, `vfx_slot[1]`, `output_fade`

These operate on the finished composition. V-4 slots chain (slot 1's
output feeds slot 2), and `output_fade` is the master dial — it runs
absolutely last so whatever you see is what gets written into the
feedback buffer for next frame. Holding fade toward black is therefore
also how you wipe the scene.

## Float-precision invariant

**No layer in the default feedback path clamps to [0,1].** This is
load-bearing — see [ADR-0001](ADR/0001-rgba32f-default-precision.md)
and [DESIGN.md](DESIGN.md#precision-over-prettification).

Historically, `physics.glsl` and `contrast.glsl` both ended with a
`clamp(rgb, 0.0, 1.0)`. Those clamps were removed (see
[ADR-0015](ADR/0015-pipeline-order-and-float-preservation.md)) because
they silently compressed the float buffer to a display-range subset of
itself, undoing most of the benefit of running the loop in RGBA32F /
RGBA16F.

Rules of thumb when authoring a new layer:

- Use `max(x, 0.0)` only to defend against NaN from `pow()` of negatives
  or `log()` near zero. That's not a dynamic-range kill.
- UV clamps (`clamp(uv, 0.0, 1.0)`) are fine — they're about sampling
  boundaries, not about colour.
- If your layer produces unbounded output (S-curve overshoot, additive
  bloom, etc.), leave it unbounded. The `decay` layer is the global
  attractor-puller, and `--precision 8` is the explicit opt-in to
  quantised behaviour for study.
- The only layer permitted to *deliberately* drop values below 0 or
  above the native range is a future tone-mapping / display-mapping
  stage, which would sit after `output_fade` and is not currently in
  the pipeline.

The blit pass (`blit.frag`) does the display-range mapping implicitly
via the default framebuffer's sRGB / 8-bit conversion. Everything
before that blit stays float.

### The one exception: NaN/Inf sanitize at `output_fade`

Unclamped float is not quite free. IEEE-754 has an absorbing state —
once a pixel becomes `NaN` or `Inf`, every subsequent operation
preserves it (`NaN * 0 = NaN`, `Inf * 0.99 = Inf`), and the poisoned
pixel survives `decay`, `couple`, `external`, `inject`, and `noise`.
Divergent dynamics (e.g. heavy contrast overshoot) can push pixels
into this state; without intervention, the field stays broken until
the user presses `C` to clear fields.

`output_fade_apply` therefore ends with a per-component ternary
select that replaces non-finite components with `0.0`. This is the
only place in the default pipeline where we conditionally rewrite a
colour component for non-range reasons, and the only layer whose
correctness the rest of the pipeline implicitly relies on. The full
HDR range is preserved; only `NaN` and `±Inf` get clobbered. See
[ADR-0016](ADR/0016-nan-inf-sanitize-at-output.md) for the
derivation and why `mix()` cannot be used here.

Authoring consequence: don't disable `output_fade`, don't add an
enable gate for it, and if you introduce a new final stage after it,
move the sanitize to the new tail.

## Hard vs soft reorder constraints

**Hard** (code will break if violated):

- `warp` and `thermal` must precede `optics` / `pixelate`. They
  produce the `src_uv` that the sample stage reads at.
- The sample stage has two mutually exclusive implementations —
  `optics_sample` (normal path) and `pixelate_apply` (active when
  `uPixelateStyle != 0`). Only one runs per frame. Both take
  `(sampler2D, vec2, ...)` and return `vec4`. See ADR-0017.
- `gamma_in` and `gamma_out` must bracket the "linear-light" stages
  as a matched pair. Splitting the pair breaks the mathematical
  intent.
- `output_fade` must be last. It's the master write level.
- Every layer's `_apply()` signature is hand-dispatched in `main.frag`.
  If you change a signature, update the dispatch.
- `L_*` layer bits are mirrored by hand between the host enum
  (`main.cpp:148-159`) and GLSL (`main.frag:74-85`). Silent breakage
  if they drift. See TODO-P2 *Unify layer-bit enum*.

**Soft** (code will compile, but dynamics / aesthetics shift):

- Internal order within UV-space stages (warp vs thermal first).
- Order of `couple` vs `external` (both are `mix()`s).
- Whether `decay` precedes or follows the mixers. Currently it
  precedes — see the feedback-write discussion above.
- Whether `inject` precedes or follows `noise`. Currently it
  precedes — so injected patterns pick up noise texture.
- Whether the two VFX slots run A-then-B or B-then-A. (They run
  A-then-B; swapping is a single-line change.)

Any soft reorder **changes the visual behaviour of every curated
preset.** Presets are not re-tuned automatically; expect an aesthetic
calibration pass after any reorder lands.

## Music → visual bridge (dropout noise)

The noise `dropout` archetype (mode 4) consumes a set of per-trigger
envelopes — `uMusKick`, `uMusSnare`, `uMusHat`, `uMusBass`, `uMusOther`
— published by the audio engine each time a voice triggers. Each
music event produces a distinctly flavoured glitch:

| Bucket | Flavour | Classification |
|---|---|---|
| kick | Wide 48-texel blocks pulled to black | sample `"bd"` / `"kick"` |
| snare | Sharp narrow white flashes | `"sn"`, `"sd"`, `"snare"`, `"cp"`, `"rim"` |
| hat | Tiny 4-texel green-tinted speckle | `"hh"`, `"oh"`, `"ch"`, `"hat"`, `"cb"` |
| bass | Medium blocks tinted by a slowly rotating hue | synth note `freq < 200 Hz` |
| other | Rare wide rainbow-coloured glitch | everything else |

Envelopes rise to the trigger's gain and decay at ~0.88 per frame
(~150 ms half-life at 60 fps). Host-side machinery lives in
`audio.cpp` (`classify_sample`, `consumeTriggerPulses`) and
`main.cpp` (the drain + envelope update alongside `Music::setScalar`).
See [ADR-0018](ADR/0018-music-to-visual-trigger-bridge.md).

## Pattern alpha channel

`inject.glsl::pattern_gen()` returns `vec4(rgb, alpha)`. Alpha is the
per-pixel injection mask:

- `alpha = 1.0` → fully inject this pixel (the default for all static
  patterns — 5 original + 5 added).
- `alpha = 0.0` → leave `col` unchanged at this pixel.

Sparse alpha lets animated / localised patterns (e.g. the pattern 10
"bouncer" — a low-res pong-ball box) ride on top of the feedback
field without wiping it. The `inject_apply` mix is
`mix(c.rgb, p.rgb, uInject * p.a)` so gaps are true no-ops.

`p.injectHoldTimer` (float seconds) on the host side lets animated
patterns override the normal `inject *= 0.85` fadeout for a declared
duration — the bouncer uses this to run for 10 seconds before
resuming normal fade.

## Per-layer quick reference

| Layer | Stage | Signature | File |
|---|---|---|---|
| warp | UV | `vec2 → vec2` | `shaders/layers/warp.glsl` |
| thermal | UV | `vec2 → vec2` | `shaders/layers/thermal.glsl` |
| optics | sample | `(sampler2D, vec2) → vec4` | `shaders/layers/optics.glsl` |
| invert | camera | inline in `main.frag` (`rgb = 1 - rgb`) | — |
| physics | camera | `vec4 → vec4` | `shaders/layers/physics.glsl` |
| gamma_in / gamma_out | analog | `vec4 → vec4` | `shaders/layers/gamma.glsl` |
| color | analog | `vec4 → vec4` | `shaders/layers/color.glsl` |
| contrast | analog | `vec4 → vec4` | `shaders/layers/contrast.glsl` |
| decay | feedback | `vec4 → vec4` | `shaders/layers/decay.glsl` |
| couple | feedback | `(vec4, vec2) → vec4` | `shaders/layers/couple.glsl` |
| external | feedback | `(vec4, vec2) → vec4` | `shaders/layers/external.glsl` |
| inject | feedback | `(vec4, vec2) → vec4` | `shaders/layers/inject.glsl` |
| noise | feedback | `(vec4, vec2, float, uint) → vec4` | `shaders/layers/noise.glsl` |
| pixelate | sample  | `(sampler2D, vec2, vec2) → vec4` | `shaders/layers/pixelate.glsl` |
| vfx_slot | post | `(vec4, vec2, int) → vec4` | `shaders/layers/vfx_slot.glsl` |
| output_fade | post | `vec4 → vec4` | `shaders/layers/output_fade.glsl` |

## Adding a new layer

Summary; walkthrough in [CONTRIBUTING.md](../CONTRIBUTING.md).

1. Create `shaders/layers/<name>.glsl` with one `<name>_apply()` function.
2. `#include` it from `shaders/main.frag`.
3. Add an `L_<NAME>` bit in `main.frag` AND the host enum in `main.cpp`.
4. Declare uniforms in `main.frag`, add `Params` fields in `main.cpp`.
5. Dispatch via `if ((uEnable & L_<NAME>) != 0)` at the appropriate
   pipeline stage.
6. Add a `LAYERS[]` entry for the toggle key, a preset read/write,
   and a help-panel section row.
7. Reload live with `\`.

## When to update this doc

- A layer is added, removed, or renamed → update the pipeline
  diagram and per-layer quick reference.
- Layer order changes → update the pipeline diagram and the
  order-rationale section; write an ADR.
- The float-precision invariant is revisited (e.g. adding a
  tone-mapping stage after `output_fade`) → update the invariant
  section; write an ADR.
- A new hard-or-soft constraint is discovered → add it to the
  constraints section.
