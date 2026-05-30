# Crutchfield Machine — Shader vs. 1984 Paper Audit

Map of every shader operation in `~/workspace/crutchfield-machine/shaders/` against Crutchfield's 1984 *Physica D* "Space-time dynamics in video feedback" equations (1, 3, 4, 7). Every claim cites `file:line`. No inference, no fabrication — where the implementation does something the paper doesn't name, it's flagged as engine-only.

The iteration is implemented in `shaders/main.frag:119–298`, orchestrated layer-by-layer with includes from `shaders/layers/`. Host wires uniforms in `main.cpp:4607–4697` (planar path) and `main.cpp:4717–4795` (volumetric path), both mirrored.

---

## Equation faithfulness

**Closest match: Equation 3 with weaknesses on the Gaussian term, plus an Equation 1 fallback when L_OPTICS is off.**

The per-pass operator implemented in `main.frag:172–298` reads as:

```
I_{n+1}(x) =
   optics( I_n( warp(x) ) )          // σ blur + chroma — see optics.glsl:85–109
   · gamma_in · color · contrast · gamma_out   // signal shaping — main.frag:243–252
   · uDecay  · borderMul             // L term — decay.glsl:29–33
   + uCouple ·  uOther(x)            // inter-field coupling, not σ blur — couple.glsl:14–17
   + uExternal · cam(x)              // camera in — external.glsl:11–14
   + noise                           // sensor floor — noise.glsl
```

Compared to paper:

- **Eq. 1** (`I_{n+1}(x) = L·I_n(x) + s·f·I_n(b·R·x)`): the warp-rotate-zoom path (`warp.glsl:5–16`) is exactly `b·R·x` (with affine pivot/translate beyond the paper). The decay (`decay.glsl:32`) is `L`. The sign `s` and gain `f` for the second term are fused into `uExternal` in a way that does **not** match the paper (see "Missing or weak" below).
- **Eq. 3** (adds `L'·⟨I_n⟩_x`, spatial Gaussian): the σ blur lives inside `optics.glsl:45–83` and is applied **only to the warped sample**, not as a separate σ-weighted term added to the loop. So the implementation conflates the warp-resample blur (σ_v, paper's vidicon intrinsic) with the σ_f focus blur the paper splits out, and there's no independent `L'·⟨I_n⟩_x` additive term.
- **Eq. 4** (temporal averaging `⟨I_n⟩_τ`): **not implemented**. The loop has exactly one history texture (`uPrev`, `main.cpp:4612`). No multi-frame `L^i` accumulator. The temporal storage `L` collapses to a single scalar `uDecay`.
- **Eq. 7** (continuous color-vector reaction-diffusion): partially. The discrete iteration is per-channel float (so the reaction part is there via `contrast.glsl:13–15` + `physics.glsl:32–61`), and the σ∇² diffusion is implicit in the optics blur, but the color matrices `L̄`/`L̄'` are reduced to a single scalar `uColorCross` averaging knob (`physics.glsl:54–58`) — no off-diagonal RGB mixing matrix and no chromatic decay distinction.

**Verdict:** the implementation is most accurately described as **Eq. 1 plus an in-loop Gaussian (closer to Eq. 3) plus engine-only extensions**, with no Eq. 4 temporal memory and a degenerate version of Eq. 7's color matrix.

---

## Variable naming · paper → shader → public cockpit

| Paper symbol | Shader uniform | Cockpit/Params field | File:line of binding |
|---|---|---|---|
| `L` (intensity decay / phosphor leakage) | `uDecay` | `decay` (default `0.995`) | `decay.glsl:32`; `main.cpp:338`, `4641` |
| `L'` (intensity signal contribution per raster) | **not present as a distinct term** | — | absent; see "Missing or weak" |
| `s` (luminance inversion sign ±1) | `uInvert` (bool gate, not ±1) | `invert` | `main.frag:231–233`; `main.cpp:369`, `4656` |
| `f` (light level / f-stop) | `uExternal` (conflated with weighting) | `external` (default `0.20`) | `external.glsl:12–13`; `main.cpp:347`, `4667` |
| `b` (zoom / spatial magnification) | `uZoom` | `zoom` (default `1.010`) | `warp.glsl:13`; `main.cpp:325`, `4623` |
| `R(φ)` (rotation matrix) | `uTheta` plus per-field sign flip | `theta` (default `0.010`) | `warp.glsl:7,11–12`; `main.cpp:325`, `4623` |
| `φ` (rotation angle) | same as `uTheta` | `theta` | `warp.glsl:7` |
| `σ_f` (focus blur) | folded into `uBlurX`/`uBlurY` | `blurX`, `blurY` | `optics.glsl:91–92`; `main.cpp:330`, `4627` |
| `σ_v` (vidicon intrinsic blur) | **not separated** — same `uBlurX/Y` | — | absent as a distinct knob |
| `L̄`/`L̄'` (color crosstalk matrix) | `uColorCross` (scalar→avg lerp) plus `uChroma` (radial CA) | `colorCross`, `chroma` | `physics.glsl:55–58`; `optics.glsl:11–43`; `main.cpp:377`, `4660` |
| noise floor | `uNoise` + `uNoiseQuality` | `noise`, `noiseQ` | `noise.glsl:15–192`; `main.cpp:343`, `4645` |
| (none — paper) | `uCouple` (Kaneko CML inter-field mix) | `couple` | `couple.glsl:14–17`; `main.cpp:345`, `4666` |
| (none — paper) | `uContrast` (S-curve about 0.5) | `contrast` | `contrast.glsl:13–15`; `main.cpp:336` |
| (none — paper) | `uGamma`, `uSensorGamma` | `gamma`, `sensorGamma` | `gamma.glsl:13–15`; `physics.glsl:40`; `main.cpp:332,375` |
| (none — paper) | `uSatKnee` (Reinhard) | `satKnee` | `physics.glsl:44–50`; `main.cpp:376` |

**Naming mismatch summary.** The cockpit and Params field names (`main.cpp:323–430`, `ui.yaml`) read as engine-flavored (`decay`, `external`, `chroma`, `contrast`, `colorCross`) rather than paper-faithful (`L`, `f`, `s`, `L̄`). Only `physics.glsl:6–20` carries paper variable annotations in comments. Nothing in the cockpit surface lets a user see "this is the paper's `L`" without reading source.

---

## Missing or weak implementations

### `L'` (incoming intensity signal contribution per raster) — fused into `uExternal`, not separable
`external.glsl:11–14`:
```glsl
vec3 cam = texture(uCam, vec2(uv.x, 1.0 - uv.y)).rgb;
return vec4(mix(c.rgb, cam, uExternal), c.a);
```
The mix means as `uExternal → 1`, feedback contribution → 0. Paper's `L'·⟨I_n⟩_x + s·f·I_n(b·R·x)` keeps the previous-frame intensity term independent of the gain on the incoming raster. Here `uExternal` is doing the work of both **f** (camera light level) and the **complement of L'** (how much of the previous frame to keep). You can't independently turn up camera intensity without dimming feedback, and there's no "intensity signal contribution" term that lives between `L·I_n` and the raster.

### `s` (luminance inversion sign) — implemented as binary gate, not ±1 multiplier
`main.frag:231–233`:
```glsl
if (uInvert == 1 && int(uFrame) - (int(uFrame)/ip)*ip == 0) {
    col.rgb = vec3(1.0) - col.rgb;
}
```
Plus `uInvertPeriod` (engine-only, `main.cpp:370–373`) to apply every Nth frame. Paper's `s ∈ {-1, +1}` would be `col.rgb = uInvertSign * col.rgb` (with offset for the 0.5 mid-point if working in [0,1]). The `1.0 - rgb` form is `s = -1` with a `+1` bias added back, which is functionally equivalent for a [0,1]-domain signal but the variable is **not exposed as a sign**, it's a gate. The N-frame periodicity has no paper analogue — it's an engine-added rhythmic control.

### `f` (light level / f-stop) — not exposed
Camera contribution amplitude lives only inside `uExternal` (above). No `uCameraGain` or `uFStop` that scales the camera read **before** the mix. Adjusting the camera's exposure is currently the user's job at the OS / V4L2 / AVFoundation level.

### `σ_f + σ_v` (focus blur vs. vidicon intrinsic blur) — not separated
`optics.glsl:85–109` uses a single `uBlurX`/`uBlurY`/`uBlurAngle` plus a `uBlurQuality` 5/9/25-tap selector. The 9-tap and 25-tap kernels (`optics.glsl:57,67–72`) **are** proper isotropic-ish Gaussians (binomial 1-2-1 and 1-4-6-4-1 separable weights, baked into a single non-separable pass). The 5-tap (`optics.glsl:50–55`) is a cross, not Gaussian. Critically:

- There is **no way to set σ_f independently from σ_v**. The blur applies once per pass at the warped sample location, so `uBlurX/Y` is effectively `σ_f + σ_v` collapsed into one knob.
- The kernel is anisotropic by design (separate X and Y radii plus `uBlurAngle`), which is *more* expressive than the paper but the paper's isotropic case isn't easily reproducible without setting `uBlurX == uBlurY`.
- A "sharpen" branch (`optics.glsl:100–106`) activates when `uBlurX` or `uBlurY` go **negative**. This is an unsharp-mask, an engine-only addition that has no equivalent in the iterated map.

### `L̄` color crosstalk matrix — reduced to scalar averaging
`physics.glsl:55–58`:
```glsl
float avg = (rgb.r + rgb.g + rgb.b) / 3.0;
rgb = mix(rgb, vec3(avg), uColorCross);
```
This is a one-parameter contraction of the 3×3 color matrix toward the rank-1 all-equal matrix. The paper's `L̄` has six independent off-diagonals (R→G, R→B, G→R, G→B, B→R, B→G). Real misconvergence is **not** symmetric and is **not** a pull toward luminance — it's a per-channel spatial offset. `uChroma` (`optics.glsl:96–97`) provides a radial per-channel offset that approximates monitor misconvergence reasonably, but it's bolted into the sampling kernel rather than expressed as a matrix.

### Noise term — present but engine-flavored
`noise.glsl:15–192` implements 5 noise modes (white, pink 1/f, heavy static, VCR, dropout). The paper mentions a vidicon thermal floor; modes 0–1 (white, pink) are paper-plausible. Modes 2–4 plus the `uMusKick/Snare/Hat/Bass/Other` audio-reactive flavoring (`noise.glsl:138–189`) are engine-only.

### Temporal averaging (`⟨I_n⟩_τ`, Eq. 4) — absent
Single history texture only (`main.cpp:4612`, `uPrev`). No accumulator over `I_{n-1}, I_{n-2}, …` with `L^i` weights. To add it would require a second history FBO and a fold-in pass. Phosphor-style temporal persistence is currently approximated only by raising `uDecay` toward 1.

### `b · R · x` only — no shear / no skew
`warp.glsl:5–16` is rotation + uniform zoom + translation about a pivot. No anisotropic zoom, no shear. Paper Eq. 1 doesn't require shear either, so this is faithful to the model, but worth flagging that the geometric transform is uniform-scale-only and any keystone-like asymmetry from a physical rig isn't representable.

---

## Engine-only additions (not in paper)

Flagged so the cockpit can label them and Sean can decide which to gate behind an "authentic mode" switch.

| Control | File:line | Notes |
|---|---|---|
| `uPivotX`, `uPivotY` | `warp.glsl:9–13`; `main.cpp:326` | Off-center rotation pivot. Real rig is fixed pivot. Engine-added. |
| `uTransX`, `uTransY` | `warp.glsl:14`; `main.cpp:327` | Pan after rotate-zoom. Engine-added. |
| `uBlurAngle` + anisotropic `uBlurX`/`uBlurY` | `optics.glsl:88–94`; `main.cpp:330` | Anisotropic / directional blur. Paper σ is isotropic. |
| Sharpen branch (negative blur) | `optics.glsl:100–106` | Unsharp mask. No paper analogue in iterated map. |
| `uContrast` S-curve | `contrast.glsl:13–15` | Electronic gain shaping. Paper's nonlinear response lives in `physics.glsl:40` (sensor gamma) + `:44–50` (sat knee). `uContrast` is double-counting in part. |
| `uGamma` display gamma | `gamma.glsl:13–15` | Distinct from sensor gamma. Engine-added display-tone control. |
| `uHueRate`, `uSatGain` | `color.glsl:11–13`; `main.cpp:334` | HSV hue rotation and sat boost. Per-frame additive hue rotation is not in the paper. Field-sign flip (`color.glsl:7–9`) is also engine-added. |
| Per-field sign flip on `theta` and `hueRate` | `warp.glsl:6–8`, `color.glsl:7–9` | "Four ring fields" symmetry-breaking. Pure engine architecture. |
| `uBorderSize`, `uBorderSoftness`, `uBorderDecay` | `decay.glsl:11–32`; `main.cpp:339–341` | Soft rounded-rect sink at edges. Engine-added attractor shaping. |
| `uCouple` (Kaneko coupled-map-lattice inter-field mix) | `couple.glsl:14–17`; `main.cpp:345` | Cited in code as "Kaneko-style", not Crutchfield. Two-field coupling is a separate body of work. |
| `uInvertPeriod` (N-frame periodic invert) | `main.frag:230–233`; `main.cpp:370` | Rhythmic flip cadence. Paper's `s` doesn't change in time. |
| `uThermAmp/Scale/Speed/Rise/Swirl` | `thermal.glsl:38–73`; `main.cpp:379–383` | Air-turbulence UV perturbation. Not in paper. Realistic "air between camera and monitor" addition. |
| `uSphereMode`, `uSphereReverb`, volumetric 3D state | `sphere.glsl:1–119`; `main.cpp:353–354` | 3D-textured volumetric feedback. Pure engine; paper is 2D. |
| `uPattern`, `uInject`, `uPatternInject`, `uShapeInject` | `inject.glsl:28+`; `main.cpp:356–367` | Initial-condition / perturbation injection. Performance feature, not paper model. |
| `uPixelateStyle`, `uPixelateBleedIdx`, `uPixelateBurnSeed` | `main.frag:206`; (pixelate.glsl) | Quantize-to-grid sampler. Engine-added aesthetic. |
| `uFxWet`, `uSourceWet` | `main.frag:212,285`; `main.cpp:349–351` | Dry/wet crossfades around the effect path and the transform stage. Pure engine. |
| `uVfxEffect[2]`, `uVfxParam[2]`, `uVfxBSource[2]` | `vfx_slot.glsl:1–25`; `main.cpp:390–392` | V-4 effect catalogue (20 effects: Strobe, Mirror, ChromaKey, Fractal, VCR, etc.). All engine. |
| `uOutFade` (bipolar to black / white) | `output_fade.glsl:20–36`; `main.cpp:395` | V-4 Output Fade dial. Engine. Also hosts NaN/Inf sanitize (`:22–27`) which is a numerical-stability addition not in paper. |
| `uBrightness` (display-only) | `blit.frag:13,85`; `main.cpp:399` | Out-of-loop scaling. Engine. |
| `uBpmPhase`, `uBpmStrobeLock`, plus all BPM-derived modulation | `main.cpp:404–429,4694–4695` | Tempo-locked beat sync for inject, hue, flash, decay-dip, invert flip. Pure engine. |
| `uMusKick/Snare/Hat/Bass/Other` | `noise.glsl:138–189`; `main.cpp:4648–4652` | Audio-reactive noise dropout flavoring. Pure engine. |
| `hi-res supersample blit` | `blit_hires.frag:1–38` | 16-tap rotated-grid AA for screenshot path. Pure engine. |

---

## Refactor proposals

Concrete moves that would tighten paper faithfulness without losing engine features. Each is independent; do whichever pass Sean wants to land first.

### R1 — Split `uExternal` into `uF` (light level) and `uLPrime` (incoming signal weight)
**File:** `shaders/layers/external.glsl:11–14`, `main.cpp:347,4667`.
Replace `mix(c.rgb, cam, uExternal)` with `c.rgb * uLPrime + cam * uF`. This restores paper's `L·I_n + L'·… + f·…` independence: camera light level becomes its own knob, and the user can dim camera without losing feedback. Default to `uLPrime = 1 - uExternal_default`, `uF = uExternal_default` for back-compat. Cockpit gains a second control `f` next to `external`.

### R2 — Expose `s` as ±1 directly
**File:** `shaders/main.frag:231–233`, `shaders/layers/physics.glsl:32–37`, `main.cpp:369,4656`.
Replace `if (uInvert==1) col.rgb = 1.0 - col.rgb` with `col.rgb = uInvertSign * (col.rgb - 0.5) + 0.5`, where `uInvertSign ∈ {-1, +1}` (or any float to allow fractional inversion as an artistic over-extension of the paper). Keep `uInvertPeriod` as a separate engine-only modulation control.

### R3 — Separate σ_f and σ_v
**File:** `shaders/layers/optics.glsl:85–109`.
Split blur into two passes: a *focus* blur (σ_f, applied to the warp result before resample) and a *vidicon* blur (σ_v, applied to the previous frame before warp). Currently both collapse into the single `optics_sample`. Adds one texture read per pass at the cost of paper faithfulness. New uniforms: `uBlurFocus`, `uBlurVidicon`. Drop the negative-blur sharpen branch (`optics.glsl:100–106`) or move it to a separate engine-flagged uniform `uSharpen`.

### R4 — Replace scalar `uColorCross` with 3×3 matrix `uLBar`
**File:** `shaders/layers/physics.glsl:55–58`, `main.cpp:377`.
Change to `rgb = uLBar * rgb` where `uLBar` is a `mat3` uniform. Default to identity. Cockpit can either expose six off-diagonals or expose two presets (`misconvergence_X`, `misconvergence_Y`) that synthesize a paper-faithful matrix. Keeps `uChroma` for radial CA since that's a spatial offset, not a color matrix.

### R5 — Add Eq. 4 temporal averaging
**File:** new uniform + second history FBO; orchestration in `main.cpp:4602–4605` and `main.frag` near `texture(uPrev, …)` calls (e.g. `:136`, `:208`).
Add `uPrev2` sampler, a `uTau` weight, and compute `L * uPrev + uTau * L^2 * uPrev2` in place of the current single-frame `c.rgb * uDecay`. Doubles the persistence cost; closes the Eq. 4 gap.

### R6 — Annotate cockpit with paper symbols
**File:** `ui.yaml`, `ui_panel.cpp:144–149`.
Add a tooltip/subtitle field in `ui.yaml` per control giving the paper symbol (`L`, `f`, `b`, `φ`, `s`, `σ`, `L̄`). Add an "authentic mode" toggle that hides engine-only controls (`thermAmp/Scale/Speed/Rise/Swirl`, `sphereMode`, `pixelateStyle`, `vfx*`, `outFade`, `borderSize/Softness/Decay`, `pivotX/Y`, `transX/Y`, `blurAngle`, `couple`, `gamma`, `contrast`, `hueRate`, `satGain`, `injectHoldTimer`, `bpm*`). The remaining surface matches Crutchfield's Eq. 1–3.

### R7 — Move `uContrast` and `uSatKnee` analysis into one tone-shaping block
**File:** `shaders/layers/contrast.glsl:13–15`, `shaders/layers/physics.glsl:44–50`.
Both apply nonlinear remapping to RGB but live in different passes. The paper's photoconductor response is one curve (`i₀ ∝ I^γ` with saturation). Currently `physics.glsl` does γ + sat knee, `contrast.glsl` does S-curve about 0.5, `gamma.glsl` does another γ at output. Collapse to a single `tone_apply(rgb, gamma, knee, contrastSlope)` function so the order and double-counting are explicit. No semantic change; clarifies the model.

---

## Footnotes

- `gamma_in_apply` (`gamma.glsl:9–11`) is currently identity — the documented "linearise for analog stages" doesn't happen. Only `gamma_out_apply` does work. Sean may want to either restore the in/out pairing or rename to `tone_out_apply` so the dead call site goes away.
- `output_fade.glsl:22–27` sanitizes NaN/Inf at the end of the loop. This is the only place the divergent-dynamics safety net lives. Worth keeping regardless of any other refactor.
- The volumetric path (`render_volume_field`, `main.cpp:4700–4796`) duplicates the planar uniform-binding block verbatim. Any new uniforms from R1–R4 need to be added in both places, or the binding should be hoisted into a helper. Cite both file:line ranges when patching.
