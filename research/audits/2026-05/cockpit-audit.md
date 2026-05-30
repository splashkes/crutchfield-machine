# DYNAMICS cockpit · code audit against Crutchfield 1984

Audit target: `~/workspace/crutchfield-machine/`
Paper: Crutchfield, J.P. *Space-time dynamics in video feedback*, Physica D 10 (1984).
Scope: classifier math in `overlay.cpp::drawMathPanel` and `main.cpp::classify_regime`; action handlers `ACT_REGIME_*` / `ACT_DYN_HALFLIFE_AXIS` / `ACT_PAD_REGIME_*` in `main.cpp`; phenomena vocabulary mapping; refactor proposals.

All file:line citations refer to commit at HEAD on the working tree.

---

## Classifier math (paper vs implementation)

### Source of truth

There are **two copies** of the classifier — the cockpit display and the engine. They share constants but live in different functions:

- Display copy: `overlay.cpp:861–872` (inside `drawMathPanel`).
- Engine copy: `main.cpp:3225–3233` (`classify_regime`, also used by the failsafe watcher and snapshot tagger).

Both compute `rho = decay · (1 − 0.02 · (blurX + blurY))` and branch on identical thresholds. Any threshold tuning has to land in both places or the cockpit will lie. **First refactor target: single source of truth.**

### ρ approximation — `rho = decay · (1 − 0.02 · (blurX + blurY))`

`overlay.cpp:861` and `main.cpp:3226`.

What the paper does: ρ is the spectral radius of the linearised feedback operator. For a pure affine zoom + decay (no blur, no noise, no coupling), ρ = `|decay| · |zoom|` for the central mode and decays with frequency for off-axis modes. Blur (a low-pass kernel) attenuates high spatial frequencies, so it lowers ρ for high-k modes — not for the DC mode.

What the code does: takes a heuristic linear knock-down `(1 − 0.02·(blurX+blurY))`. With the engine's actual blur range of 0..12 px (`main.cpp:1016`), that's a multiplier from 1.00 down to 0.76. **Zoom is not in the formula at all** (`overlay.cpp:861` ignores `cur.zoom`). The paper's whole point is that `decay · zoom > 1` causes exponential amplification of any seeded mode — that's the "bursts" phenomenon. So the cockpit will read a zoomed-out, high-decay system as "STABLE" when it's actually a divergent log-spiral pump.

Sub-cases where the heuristic is *defensible*:
- Zoom ≈ 1.0 (the engine's default), no rotation, blur off → ρ ≈ decay, which is correct for the DC mode.
- Zoom ≈ 1.0, mild blur → the knockdown approximates spectral-radius reduction for the dominant low-k mode.

Sub-cases where the heuristic is *wrong*:
- Zoom ≠ 1.0 (any spiral preset): zoom multiplies/divides the spectral radius. Missing entirely.
- Theta ≠ 0 with zoom ≠ 1: log-spiral regime per the paper — ρ depends on both. Missing.
- High blur with low decay: blur attenuates Fourier modes Gaussian-fashion; the linear `0.02·blur` underestimates the effect at low decay and overestimates it at high decay.

**Verdict:** ρ is correct only at zoom = 1, theta = 0, low blur — the trivial "no warp" sub-case the paper barely discusses. Outside that, it's a wall thermometer that tells you the room's temperature in Spanish.

### Regime thresholds

| Threshold | Code | Maps to paper… |
|---|---|---|
| `rho > 1.001` → DIVERGENT | `overlay.cpp:868`, `main.cpp:3228` | Linear instability of the trivial fixed point. Paper §III.A. Sane, modulo the ρ issue above. |
| `rho > 0.998` → MARGINAL | `overlay.cpp:869`, `main.cpp:3229` | Paper has no "marginal" label — closest is "intermittency" or "boundary crisis". Closer mapping = noise-modulated stability of a marginal fixed point. See *Regime label fidelity* below. |
| `Kc > 0.6` → CHAOTIC | `overlay.cpp:870`, `main.cpp:3230` | Hand-tuned to the Kaneko coupling regime where partner-field mixing dominates. The paper itself doesn't have a `Kc` parameter — coupling is a downstream addition from Kaneko 1989. This is **not** a paper-derived threshold. |
| `Kc > 0.3` → TURBULENT | `overlay.cpp:871`, `main.cpp:3231` | Same: Kaneko-CML terminology, not Crutchfield. |
| else | → STABLE | OK. |

The branch order is also load-bearing: `rho > 1.001` and `rho > 0.998` are checked *before* `Kc`, so a high-coupling chaotic system also runs MARGINAL/DIVERGENT if ρ creeps above 0.998. This is probably the intended priority but it's worth flagging — a system can be aperiodic-chaotic (paper's "chaotic attractor") while ρ ≈ 1, and the cockpit will say MARGINAL.

### "Walk to chaos" mapping (`ACT_REGIME_DISTANCE_AXIS`)

`main.cpp:3416–3441`. Piecewise linear interpolation over (Kc, decay, noise):

| t | Kc | decay | noise |
|---|---|---|---|
| 0.0 | 0.05 | 0.97 | 0.001 |
| 0.5 | 0.35 | 0.985 | 0.005 |
| 1.0 | 0.70 | 0.995 | 0.020 |

The breakpoint at t=0.5 is intentional: it's exactly the `Kc > 0.3` TURBULENT threshold (within rounding) and t=1.0 is the `Kc > 0.6` CHAOTIC threshold. So the slider is *monotonic in regime code* by construction — drag right, regime advances. That part works.

The display side (`overlay.cpp:898–901` and `953–957`) computes the inverse mapping from current Kc back to t to position the "you are here" marker. The inverse is also piecewise but **only on Kc**, ignoring decay and noise. So if you set Kc=0.35 by hand without touching decay/noise, the bar reads t≈0.5 even though the underlying system isn't on the regime.distance trajectory. Cosmetic but worth noting.

Rationale per the in-code comment ("Piecewise path through (K_c, decay, noise) precomputed to hit the regime thresholds at the right t values", `main.cpp:3418–3419`): correct for the implementation's classifier. It's a self-consistent loop with the classifier's own thresholds, not a paper-derived path.

### Half-life formula

`overlay.cpp:857–859`: `halflife_frames = log(0.5) / log(decay)`, `halflife_sec = halflife_frames / 60`.

Mathematically correct for a pure scalar exponential decay: `c · decay^n = 0.5 c` ⇒ `n = log(0.5) / log(decay)`. The 60 fps assumption matches the engine's reference (`main.cpp:3392` comment: "60 fps reference") and matches the reverse mapping in `ACT_DYN_HALFLIFE_AXIS` (`main.cpp:3394`: `d = pow(0.5, 1/h_frames)`).

Relation to the paper's L parameter: the paper's L is the "memory length" of the iterated map. For an additive-decay pixel model, L is exactly `1 / |ln(decay)|` characteristic decay frames; half-life is `L · ln(2)`. The implementation gives half-life directly, which is the right operator-friendly choice. Half-life and L are interconvertible, so this is fine — just note the cockpit shows half-life, not L. If you want to match paper vocabulary, add an L readout next to half-life: `L = halflife / ln(2)` (sec or frames).

The slider's log mapping (axis 0..1 → seconds 0.05..10) at `main.cpp:3391` (`h_sec = 0.05 · 200^v`) and its inverse in the display (`overlay.cpp:986`: `tHL = ln(h_sec / 0.05) / ln(200)`) are consistent.

---

## Regime label fidelity (Table II mapping)

Paper's Table II behavioural vocabulary vs cockpit labels:

| Cockpit label | Paper attractor class | Match? | Notes |
|---|---|---|---|
| STABLE | "equilibrium image" → fixed point | Strong | Cockpit's STABLE region (`rho < 0.998` and `Kc < 0.3`) is exactly the fixed-point basin. Good. |
| TURBULENT | (not in Table II) | **Mismatch** | The paper does not use "turbulent". Closest analogues: "temporally repeating images" (limit cycle) or "spatially decorrelated dynamics (e.g. dislocations)" (quasi-attractor). The cockpit's TURBULENT is just "moderate Kaneko coupling" — neither paper class. Suggest rename to "OSCILLATING" or "QUASI-ATTRACTOR" depending on which you mean. |
| CHAOTIC | "temporally aperiodic images" → chaotic attractor | Reasonable | Only reasonable if rotation/zoom are tuned to the chaotic regime. With Kc as the sole proxy (`Kc > 0.6`), it conflates Kaneko-CML chaos with Crutchfield-iterated-map chaos. Different mechanisms; same label. Document the distinction or split the label. |
| MARGINAL | "random relaxation oscillation" → limit cycle with noise-modulated stability | Partial | The paper's marginal class is *noise-modulated*. The cockpit's MARGINAL is purely a ρ band (0.998 < ρ < 1.001), ignoring noise. A high-noise system at ρ ≈ 0.99 is the paper's "marginal" behaviourally but the cockpit calls it STABLE. **Mismatch in driver: should be `rho ∈ band` AND `noise > threshold`.** |
| DIVERGENT | (not in Table II) | Useful | Not a paper category — but a useful operational one because the engine can blow up. The paper assumes bounded luminance; the engine clamps but lets you watch it saturate. Keep as an engine-safety label, not a paper one. |

**Paper categories NOT represented in cockpit labels:**

- "spatially decorrelated dynamics" (quasi-attractor with local fixed/limit/chaotic): the paper's dislocations regime. No label.
- "spatially complex image" (spatial attractor): no label.
- "spatially + temporally aperiodic" (nontrivial combination): no label, but probably what users get when they crank coupling + noise.

---

## Named phenomena · what's classifiable, what's missing

### Currently classifiable from parameters alone (no image analysis)

**Symmetry locking (n-fold spatial symmetry).** Paper §IV.B: rotation angle θ = 2π/n produces n-fold rotational symmetry in steady state. Engine theta range is ±0.08 rad (`main.cpp:986`), so the accessible n-values are roughly n ≥ ⌈2π/0.08⌉ = 79. To hit visible low-n symmetries (n = 4, 5, 6, 8) you'd need θ ∈ {π/2 ≈ 1.57, 2π/5 ≈ 1.26, π/3 ≈ 1.05, π/4 ≈ 0.78} — way outside the current ±0.08 range. **Either the theta range needs widening for symmetry-locking to be reachable, or the cockpit should expose a "symmetry mode" that overrides theta to a small set of paper-canonical n-fold values.** Detector formula: `n = round(2π / theta)`, label "n-fold locked" when |2π/theta − round(2π/theta)| < 0.02. Computable from params alone.

**Logarithmic spirals.** Paper §IV.A: zoom ≠ 1 AND rotation ≠ 0 produces log spirals. Pitch angle = `atan(ln(zoom) / theta)`. With current ranges (zoom 0.92..1.08, theta ±0.08), a clear spiral exists whenever `|zoom − 1| > 0.005` AND `|theta| > 0.005`. Classifiable directly. Cockpit currently shows zoom and theta as numbers but never labels the phenomenon.

**Bursts (high zoom amplifies noise exponentially).** Paper §V.A. Condition: `zoom > 1` (or, more generally, ρ_effective > 1 on some mode) AND noise floor > 0. Classifiable as: `(zoom > 1.005) AND (decay · zoom > 1.0)` AND `noise > 0.001`. The current DIVERGENT label catches the *result* once the burst saturates, but not the *condition* of imminent bursting.

**Pinwheels (luminance inversion + specific rotation).** Paper §IV.D. Requires a luminance-inversion stage. Doesn't appear to exist as a current engine layer (grep `invert|negate` against `shaders/layers/`). If you add a "luminance invert" toggle (one-liner shader), the cockpit can label PINWHEEL when invert=on AND theta in {±π/n} bands.

### Currently impossible without image analysis

**Dislocations (broken stripe symmetry → quasi-attractor).** Requires detecting a defect in a striped or spotted spatial pattern. Needs frame readback + FFT or wavelet analysis. Not in the codebase, not cheap to add at 60 fps. Could be approximated by an on-GPU autocorrelation pass once per second.

**Color waves (Belousov-Zhabotinsky-like reaction-diffusion).** Requires temporal Fourier analysis of hue distribution, or pixel-class tracking. Same constraint as dislocations.

**Distinguishing limit cycle vs chaotic attractor temporally.** Requires a temporal autocorrelation of a global image statistic (mean luminance, dominant hue, centroid drift). The `MathSample` ring (`overlay.h:79`, `MATH_RING_CAP`) already captures parameter history but **not image statistics**. Adding one or two per-frame readbacks (mean luminance, mean saturation, centroid) would unlock cheap autocorrelation-based period detection.

### Useful additions to the cockpit

In order of effort × payoff:

1. **n-fold symmetry readout** (zero image analysis, pure param formula). Cheap, paper-aligned, immediately useful for visual presets.
2. **Spiral pitch readout** (same). Two numbers: pitch angle and chirality.
3. **Burst-risk indicator** (param formula on zoom × decay × noise). Distinct from DIVERGENT — predictive, not reactive.
4. **Mean-luminance ring + period detector** (one GPU readback per frame, ~3 ms). Unlocks limit-cycle vs chaotic distinction.
5. **Luminance-invert toggle + pinwheel label** (new shader layer + one classifier line).
6. **On-GPU 2D autocorrelation peak detection** (~30 Hz). Unlocks dislocations and color-wave detection. Two-to-three-day build.

---

## Action vocabulary (cockpit → engine → paper terms)

| Cockpit control | Action ID | Engine effect | Paper term |
|---|---|---|---|
| "walk to chaos" slider | `ACT_REGIME_DISTANCE_AXIS` (`overlay.cpp:597`) | Sets `(Kc, decay, noise)` along precomputed curve (`main.cpp:3416–3441`) | Not paper-aligned — uses Kaneko's `Kc`, paper's decay, paper's noise. Slider name "to chaos" is journalistic; the trajectory is "increasing CML coupling along a path through this engine's classifier thresholds". |
| "memory" half-life slider | `ACT_DYN_HALFLIFE_AXIS` (`overlay.cpp:600`) | Sets `decay = 0.5^(1/h_frames)` (`main.cpp:3388–3400`) | Paper's L parameter, transformed to half-life. Faithful, just relabeled. |
| Jump button STABLE | `ACT_REGIME_SET` value 0 (`overlay.cpp:614`) | Blends params 70/30 toward (decay=0.97, Kc=0.05, noise=0.001) (`main.cpp:3449`) | Paper's fixed-point basin. **OK** — lands in the right behavioural class assuming zoom and theta aren't already pulling against it. |
| Jump button TURBULENT | `ACT_REGIME_SET` value 1 | Blends toward (decay=0.985, Kc=0.45, noise=0.008) (`main.cpp:3450`) | Paper has no "turbulent" — this lands in CML coupling regime which is closer to "quasi-attractor with local limit/chaotic" if anything. **Mismatch.** |
| Jump button CHAOTIC | `ACT_REGIME_SET` value 2 | Blends toward (decay=0.995, Kc=0.70, noise=0.020) (`main.cpp:3451`) | Paper's "chaotic attractor" — temporally aperiodic. **Plausible** but only via the CML mechanism, not via the iterated-affine-map mechanism the paper describes. |
| Jump button MARGINAL | `ACT_REGIME_SET` value 3 | Blends toward (decay=0.998, Kc=0.15, noise=0.001) (`main.cpp:3452`) | Paper's "limit cycle with noise-modulated stability" requires NOISE. This preset pushes noise *down* to 0.001 and pushes decay close to 1. So it's the *substrate* of marginal behaviour but missing the noise modulation that defines the class. **Partial mismatch.** Suggest: bump noise to 0.005–0.010 for this preset. |
| INVERT button | `ACT_REGIME_INVERT` (`overlay.cpp:617`) | Bumps Kc to the other side of nearest boundary in {0.30, 0.60} (`main.cpp:3463–3477`) | No direct paper analogue. This is an engine UX convenience. The name is OK but document that "invert" means "cross the nearest classifier boundary", not "invert luminance" (which is what a Crutchfield reader might expect from §IV.D pinwheels). **Naming collision risk.** Suggest rename to "CROSS BOUNDARY" or "FLIP REGIME". |
| Compass pad | `ACT_PAD_REGIME_X` / `_Y` (`overlay.cpp:609–610`) | Polar blend between the four regime tuples (`main.cpp:3480–3515`) | Same caveat as jump buttons: the four cardinals are 70/30 blends of the same partial-mismatch presets. Angle picks quadrant, radius picks intensity. Pad geometry (NE=STABLE, NW=TURB, SW=CHAOS, SE=MARG) is documented in code comments (`main.cpp:3496–3497`, `overlay.cpp:1042–1046`). |
| FAILSAFE | `ACT_THEATER_FAILSAFE` (`overlay.cpp:620`) | Toggles watcher that recalls last STABLE snapshot if DIVERGENT for >2s (`main.cpp:3297–3325`) | Engine safety. Not a paper concept. Useful, keep. |
| MATH ECHO | `ACT_MATH_ECHO_TOGGLE` (`overlay.cpp:623`) | Publishes `/cma/math/{rho,halflife,diffusion,coupling,noise/db,regime}` at 30 Hz (`main.cpp:3339–3368`) | Engineering tap, not a paper concept. |
| Snapshot save (1..4) | `ACT_SNAPSHOT_SAVE` (`overlay.cpp:626`) | Snapshots tagged with `classify_regime` result (`main.cpp:3245`) | Engine UX. |
| RECALL slot 1 | `ACT_SNAPSHOT_RECALL` value 1 (`overlay.cpp:632`) | Hard-coded to slot 1, not regime-tagged | **Misleading label.** The cockpit button text is "RECALL slot 1" but it lives under a header that implies a stable-recall pattern. The code comment at `overlay.cpp:628–631` already flags this: "We don't have a regime-tagged recall action exposed, so fire snapshot.recall on slot 1 as a sane default". Add `ACT_SNAPSHOT_RECALL_LAST_STABLE` that calls the existing `snapshot_last_with_regime(0)` helper used by failsafe (`main.cpp:3306`). |

---

## Refactor proposals

### R1 — single source of truth for the classifier

`overlay.cpp:861–872` and `main.cpp:3225–3233` are duplicate classifier code. Expose `classify_regime()` (currently in an anonymous namespace, `main.cpp:3222`) via a small header `regime.h`, or pass the regime code into `MathSample`. The display copy keeps drifting from the engine copy a manual sync at a time. One copy.

### R2 — fix or scope the ρ approximation

Two options:

**(a) Honest scope.** Rename `ρ` to `ρ̃` ("ρ estimate, central mode, no warp") and add a tooltip: *valid at zoom ≈ 1, theta ≈ 0, blur ≤ 6 px*. Keep the heuristic, label its limits. Cheapest fix.

**(b) Better formula.** For the dominant low-k mode of the iterated affine + Gaussian-blur + decay map:

```
ρ_dom ≈ decay · |zoom| · exp(−2π² · σ_eff² · k_dom²)
```

where `σ_eff² = 0.5 · (blurX² + blurY²) / N²` (N = image size in px) and `k_dom` is a chosen probe wavenumber (e.g. 4 cycles per image). Costs three multiplies and an exp. Add `cur.zoom` to the formula. Removes the "zoom invisible" bug.

I'd go with (b). It's still an approximation but it covers the spiral and burst cases the paper's whole methodology is about.

### R3 — rename regime labels to paper vocabulary

Suggested map:

| Current | Proposed | Driver |
|---|---|---|
| STABLE | FIXED POINT | unchanged |
| TURBULENT | LIMIT CYCLE | requires limit-cycle detection (see R7) — short-term, alias to "OSCILLATING (CML)" |
| CHAOTIC | CHAOTIC | unchanged |
| MARGINAL | NOISE-MARGINAL | and add noise > 0.003 to the gate (see R4) |
| DIVERGENT | UNBOUNDED | engine-only label, document as such |

Add an in-cockpit hover/tooltip linking each label to the Table II row. Two-line subtitle under the regime bar.

### R4 — make MARGINAL noise-aware

Current gate (`main.cpp:3229`): `rho > 0.998`. Paper's marginal class is noise-modulated. New gate:

```cpp
if (rho > 1.001f) return 4;                                  // UNBOUNDED
if (rho > 0.995f && p.noise > 0.003f) return 3;              // NOISE-MARGINAL
if (rho > 0.998f) return 3;                                  // edge case: also marginal
```

Also bump the MARGINAL preset noise from 0.001 → 0.008 (`main.cpp:3452` and `main.cpp:3502`) so jump-MARGINAL actually lands in the paper's class.

### R5 — new derived metrics in the cockpit

Add to `drawMathPanel` (`overlay.cpp:910–935` two-column row, after K_c and noise):

```
n-fold         n = 2π / |theta|     (label "locked" when fractional part < 0.02)
spiral pitch   atan(ln(zoom) / theta)  (label CW/CCW from sign)
burst risk     decay · zoom · (1 + noise·100)  (label "imminent" > 1.0)
L (frames)     halflife / ln(2)
```

All zero-cost — pure params. Adds four labels users actually want when reading the paper alongside the cockpit.

### R6 — luminance-invert layer + PINWHEEL label

Shader: new `layers/invert.glsl` with `vec3 invert_apply(vec3 c) { return vec3(1.0) - c; }` and a `uInvert` toggle. Action: `ACT_LUMA_INVERT_TOGGLE`. Cockpit: new regime label PINWHEEL when invert=on AND |theta| > 0.02. One afternoon's work.

### R7 — image-stat sample ring for limit-cycle detection

Extend `MathSample` (`overlay.h:79`) with one field: `float meanLuma`. Sample once per frame via a 1×1 mipmap readback or a downsampled FBO. With ~4 seconds of history (already the `MATH_RING_CAP` budget), run a one-pass autocorrelation and report dominant period in beats and frames. Label TURBULENT → LIMIT CYCLE when a clean period exists, → CHAOTIC when autocorrelation is flat.

Cost: one extra GPU readback per frame (~0.3 ms on a modern Mac), one autocorrelation pass (~0.1 ms for 240 samples). Acceptable.

### R8 — rename "INVERT" button → "CROSS BOUNDARY"

`overlay.cpp:1022` button label, `main.cpp:3463–3477` handler. Avoids the naming collision with luminance inversion (R6 would otherwise make "INVERT" overloaded). Also rename the action `ACT_REGIME_INVERT` → `ACT_REGIME_CROSS_BOUNDARY` for clarity. Mechanical rename.

### R9 — wire a real "RECALL last STABLE" action

`overlay.cpp:628–632` already documents this gap. Add `ACT_SNAPSHOT_RECALL_LAST_STABLE` that calls `snapshot_last_with_regime(0)` (already used by failsafe at `main.cpp:3306`). Bind the cockpit button to it. Removes the lying "RECALL slot 1" label.

### R10 — add an L (memory length) readout next to half-life

`overlay.cpp:920–923`. Half-life is one number, L is the other. `L_sec = halflife_sec / ln(2)`. Two characters of code, paper-faithful. Helps when readers cross-reference paper notation.

### R11 — threshold widening for symmetry locking

Current theta range `±0.08 rad` (`main.cpp:986`) cannot reach n-fold symmetries for n < ~79. Either:

**(a)** Widen the slider range to ±π (full rotation) and let users hit n = 4, 5, 6, 8 directly. May break existing presets.

**(b)** Keep the slider tight, add a discrete "symmetry mode" action `ACT_THETA_NFOLD_AXIS` that maps 0..1 → discrete n ∈ {2, 3, 4, 5, 6, 8, 12, 16, 24, 36} and sets `theta = 2π / n`. New cockpit button row. Doesn't break presets, makes the paper's named phenomenon a one-click jump.

I'd ship (b). It composes well with the spiral pitch readout (R5) and gives the cockpit a button that says "symmetry lock 6-fold" — instantly paper-aligned.

### R12 — split the classifier into "iteration" vs "coupling" mechanisms

The current classifier conflates two different mechanisms: iterated-affine-map dynamics (Crutchfield 1984, parameters: zoom, theta, decay, blur, noise) and CML coupling (Kaneko 1989, parameter: Kc). They're both real and both interesting, but they produce different attractor *types*. Consider two regime axes:

- **Iteration regime**: derived from ρ_dom and noise → {FIXED, ORBIT, CHAOTIC, UNBOUNDED}.
- **Coupling regime**: derived from Kc alone → {DECOUPLED, MIXED, DOMINATED}.

Render them as two strips in the regime bar instead of one. Lets users see "I'm in a fixed-point iteration regime but with dominated coupling" — which is a real situation the current one-bar UI can't show.

---

## Quick wins for a same-day pass

If you only have an hour:

1. R1 (single source of truth) — 10 min.
2. R5 (n-fold + spiral pitch + L readouts) — 30 min.
3. R8 (rename INVERT → CROSS BOUNDARY) — 10 min.
4. R9 (real RECALL last STABLE) — 10 min.

If you have a day:

5. R2(b) (better ρ formula with zoom) — 1 h.
6. R4 (noise-aware MARGINAL gate + preset fix) — 30 min.
7. R3 (label rename + tooltips) — 1 h.
8. R11(b) (symmetry-mode action + cockpit row) — 2 h.

If you have a week:

9. R6 (invert layer + pinwheel label) — 1 day.
10. R7 (mean-luma ring + period detector) — 1 day.
11. R12 (split regime axes) — 1 day.
12. Eventually: on-GPU 2D autocorrelation for dislocations and color waves — 2–3 days.
