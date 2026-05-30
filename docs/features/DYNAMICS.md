# DYNAMICS · the operator cockpit

Press **M** (or send OSC `/cma/math/toggle`) to open the DYNAMICS panel on the right side of the screen. It is an interactive cockpit over the feedback engine, framed as a dynamical system in the language of Crutchfield 1984 ("Space-time dynamics in video feedback," Physica D 10, 229–245).

Every visible element is clickable. Sliders drag. Buttons fire. The compass pad reads X and Y. Snapshot rows save and recall complete parameter sets in one click. The cockpit and the raw parameter editor coexist · the editor (press H or Tab) is the low-level surface for individual knobs; DYNAMICS is the semantic surface for steering the loop as a whole.

## What it shows

Four sections, top to bottom.

### 1 · regime bar

A horizontal bar coloured by classifier output, with a "you are here" marker. The classifier is driven by spectral-radius and coupling estimates derived from current parameters. See [the regime table below](#regime-classification).

### 2 · system characterization (two-column readout)

```
ρ (spectral radius)      0.9750            half-life      0.42 s
K_c (coupling)           0.050             noise          -53.9 dB
```

ρ is a stability proxy. Below 1 the loop decays toward a fixed point; near 1 it sits at the edge; above 1 the loop is divergent (paper's "attractor at infinity"). The estimate is `decay × (1 − 0.02 × (σ_x + σ_y))` · a linear approximation valid near zoom ≈ 1, theta ≈ 0. It does not capture spiral or burst regimes that need the rotation matrix.

Half-life is `log(0.5) / log(decay)` frames, expressed in seconds at 60 fps. This is the time constant of paper's L parameter expressed in human terms.

### 3 · walk-to-chaos slider · memory slider

Two semantic axes. Drag walk-to-chaos to move the loop along the bifurcation axis (paper's "arc punctuated by bifurcations"). Drag memory to set how long a feature survives, in seconds.

### 4 · jump buttons · compass pad · snapshots

Four jump buttons snap the engine into one of the named regimes below. Compass pad drives a 2D parameter map (currently energy + memory; planned: rotation + zoom for the spiral regime). FAILSAFE clamps the loop before divergence. MATH ECHO publishes the live state over OSC for an external instrument or controller. Snapshots capture and recall a complete parameter set.

## Regime classification

The engine uses operator-friendly names with paper-faithful subtitles.

| Engine label | Paper term | Trigger | Colour |
| --- | --- | --- | --- |
| STABLE | fixed point | ρ < 0.998 AND K_c < 0.3 | green |
| TURBULENT | limit cycle / quasi-attractor | K_c ≥ 0.3 | orange |
| CHAOTIC | chaotic attractor | K_c ≥ 0.6 | red |
| MARGINAL | bifurcation edge | ρ ≥ 0.998 | orange |
| DIVERGENT | attractor at infinity | ρ > 1.001 | red |

Refer to Crutchfield Table II for the full taxonomy. Three behaviour types named in the paper that the current classifier does not yet expose · logarithmic spirals (zoom ≠ 1 + theta ≠ 0), pinwheels (luminance inversion + specific theta), dislocations (broken stripe symmetry · requires image-domain analysis). These are tracked as follow-up issues.

## Parameter ↔ paper symbol mapping

See [../../CREDITS.md](../../CREDITS.md) for the canonical table. Short form below.

| Paper | Engine | Cockpit |
| --- | --- | --- |
| L | decay | "memory" slider, half-life readout |
| s (±1) | invert | INVERT action (luminance sign) |
| f | external | the iPhone or camera gain |
| b | zoom | zoom param |
| R(φ) | theta | rotation angle |
| σ_f + σ_v | blurX, blurY | diffusion D readout |
| L̄, L̄' off-diag | chroma, couple | colour cross, coupling |
| noise | noise | noise floor readout |

## Toggle bindings

```ini
[keyboard]
app.math = M
app.math = F8                ; alternative

[osc]
app.math = osc:/cma/math/toggle

[midi]
app.math = note:108 ch=9     ; LC v1 right-side Record button
```

## Performance

Per-frame cost: ~5 µs to push a sample into the ring buffer plus ~50 µs for the panel render. Negligible.

## Layout

Panel width: `min(880, framebuffer_width × 0.50)` · scales gracefully on small or huge displays. Right-aligned with 24 px margin. Translucent dark background (`#080C14 @ 94%` alpha) with a 4-pixel cyan accent stripe at the top.

## Implementation pointers

- `overlay.h` · `MathSample` struct, `mathPushFrame`, `toggleMath`, `mathVisible`, hit list, drag state.
- `overlay.cpp` · `drawMathPanel()` (cockpit render), `drawSparkline()`, mouse handlers.
- `input.h` / `input.cpp` · `ACT_MATH_TOGGLE`, `ACT_REGIME_*`, `ACT_DYN_HALFLIFE_AXIS`, `ACT_PAD_REGIME_X/Y`, `ACT_THEATER_FAILSAFE`, `ACT_MATH_ECHO_TOGGLE`.
- `main.cpp` · `apply_action` dispatcher for all the above, plus `mathPushFrame` called once per frame with the current `Params`. Regime classifier `classify_regime` lives here; the panel's display classifier shares the same logic.

## Related

- [META_CONTROLS.md](META_CONTROLS.md) · the action vocabulary for semantic control
- [MACROS_SNAPSHOTS.md](MACROS_SNAPSHOTS.md) · snapshot system
- [../../CREDITS.md](../../CREDITS.md) · Crutchfield 1984 + symbol mapping
- [../../research/PHILOSOPHY.md](../../research/PHILOSOPHY.md) · long-form paper grounding
