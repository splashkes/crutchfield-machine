# Mathlab dashboard — the dynamical-system analytical view

Press **M** (or bind `app.math` to any input) to toggle a translucent panel on the right side of the screen that shows the feedback engine as a discrete-time dynamical system.

**Mathlab is analysis only.** It shows the math, the regime classifier, and recent parameter history as sparklines. It does NOT edit parameters directly — that's the parameter editor's job (press H or Tab to open it).

Why split: the parameter editor in `ui_panel.cpp` is already a complete editor with sliders, mouse-drag, value display, mouse hit-test, and YAML-driven layout. Building a parallel editor inside Mathlab was duplicative. Mathlab's unique value is the analytical layer; the editor stays where it was.

What you can do with the math layer directly:
- **READ** the system characterization (this doc, here)
- **WATCH** parameter history in sparklines (this doc, here)
- **CONTROL** dynamical quantities via meta-control actions (see [META_CONTROLS.md](META_CONTROLS.md))

## What it shows

Three sections, top to bottom.

### 1. System characterization

Analytical quantities derived from the current parameter set. These are predictions, not measurements — they tell you what the math says about where the system is *now*, before you have to wait and see what happens on screen.

```
SYSTEM CHARACTERIZATION
  regime         : STABLE
  rho (spectral radius)  : 0.9750
  memory half-life       : 27.4 frames  (0.46 s @ 60fps)
  diffusion D            : 0.5000  (blur^2 / 2)
  coupling K_c           : 0.0500
  noise floor            : 0.002 (-53.9 dB)
  hue rotation           : 64.80 deg/s
```

**Regime** classifies the system using thresholds on ρ and K_c:

| Regime | Color | Trigger |
| --- | --- | --- |
| STABLE     | green   | ρ < 0.998 AND K_c < 0.3 |
| TURBULENT  | orange  | K_c ≥ 0.3 |
| CHAOTIC    | red     | K_c ≥ 0.6 |
| MARGINAL   | orange  | ρ ≥ 0.998 |
| DIVERGENT  | red     | ρ > 1.001 |

**Spectral radius ρ** is an estimate of the dominant eigenvalue of the linearized feedback iteration:
```
ρ ≈ decay × (1 − 0.02 × (blur_x + blur_y))
```
ρ < 1 ⇒ decaying memory (stable); ρ ≈ 1 ⇒ marginal; ρ > 1 ⇒ exponential growth.

**Memory half-life** = `log(0.5) / log(decay)` in frames. `decay = 0.99` → 69 frames → 1.15 s at 60 fps. This is how long a feature in the feedback takes to fade by half (in the absence of injection or coupling).

**Diffusion D** = `0.5 × (σx² + σy²) × 0.5` — the effective diffusion coefficient from the blur passes. Higher D = features spread faster, sharper detail decays sooner.

**Coupling K_c** is the cross-field coupling strength. Below 0.3 the system behaves roughly linearly; above 0.6 it becomes chaotic in the Lyapunov sense.

**Noise floor** in dB ≈ `20 log10(noise)`. Physical analog: how much stochastic forcing is driving the system per frame.

**Hue rotation** in deg/s = `hueRate × 60 × 360`. Lets you set a meaningful rotation rate even though the raw parameter is in radians-per-frame.

### 2. Parameter readout with sparklines

13 continuous parameters in two columns:

```
PARAMETERS
  decay    lambda  = 0.9900    [sparkline]
    memory term, 1.0 = perfect recall
  blur X   sigma_x = 1.000     [sparkline]
    horizontal diffusion px
  blur Y   sigma_y = 1.000     [sparkline]
    vertical diffusion px
  chroma   chi     = 0.0020    [sparkline]
    wavelength dispersion
  ...
```

Each row shows:
- Label
- Mathematical symbol (decay = λ, blur = σ, chroma = χ, gamma = γ, noise = ε, couple = K_c, theta = θ)
- Current value (4 digits of precision)
- One-line interpretation
- An auto-scaling sparkline of the last ~6 seconds (360 samples)

The sparklines auto-scale per parameter, so each one's vertical range shows the parameter's recent activity. If you've been steady on `decay = 0.99` for 6 seconds, the sparkline is a flat line; if you tapped it down to 0.5 and back, you see a dip-and-recover curve.

13 parameters covered: decay, blur X/Y, chroma, gamma, sat, contrast, noise, couple, external, out fade, zoom, theta.

### 3. (Optional future) Phase portrait

Not yet implemented. A future addition: a 2D scatter showing one pixel's trajectory through (R, G) or (luminance, change) space — the system's "attractor" visualization in colour-space.

## Toggle

```bash
# Keyboard: M (default binding)
# OR bind to anything via app.math action:

[osc]
app.math = osc:/cma/math/toggle

[midi]
app.math = note:108 ch=9       ; LC v1 right-side Record button

[keyboard]
app.math = F8                   ; alternative key
```

## Why analytical instead of empirical

We considered measuring properties of the rendered image (Lyapunov exponent via perturbed shadow simulation, FFT spectral analysis, histogram entropy). Decided against for v1:

- **GPU readback is expensive** — even a 64×64 downsample is ~1 ms of stall per frame.
- **Analytical predictions are more useful for control.** They tell you what the system will do *if you keep doing what you're doing*, before the consequence is visible on screen. Measurements describe; predictions inform.
- **The math is the interface.** Showing ρ, half-life, diffusion as numbers makes the parameter-space topology legible. You start to feel the relationship between knob position and dynamical regime.

A future enhancement could add an empirical section (histogram, dominant frequency, on-screen entropy) — separate from but parallel to the analytical view.

## Reading the panel

**Watching ρ approach 1.0**: the system is about to blow up. Lower decay or increase noise (which damps).

**Watching K_c climb past 0.3**: you're entering turbulent regime. Expect ergodic mixing — features get smeared into each other.

**Watching K_c climb past 0.6**: chaotic. Small parameter changes have big visual effects. Don't try to "tune" here — embrace it.

**Watching half-life shrink to single digits**: feedback is barely feeding back. The image refreshes nearly every frame. Good for snappy, gestural visuals; bad for ambient drone.

**Watching half-life climb past 100 frames (1.7 s)**: deep memory regime. Marks/strokes persist visibly across seconds. Good for ambient/drone; bad for fast-changing performance because the residue accumulates.

## Performance

Cost per frame: ~5 µs to push a sample into the ring buffer + ~50 µs for the panel render (text + sparklines). Negligible.

## Layout details

- Panel width: `min(560, framebuffer_width × 0.42)` — scales gracefully on small or huge displays.
- Position: right edge, with 20 px margin.
- Translucent dark background (`#0A0E16 @ 85%` alpha) with a 2-pixel cyan accent stripe at the top.
- Two-column layout: parameter readout left (280 px), sparklines right (the rest).
- Text uses the same stb_easy_font path as the existing HUD — no extra GL state.

## Implementation pointers

- `overlay.h`: `MathSample` struct, `mathPushFrame()`, `toggleMath()`, `mathVisible()`, ring buffer + cap
- `overlay.cpp`: `drawMathPanel()`, `drawSparkline()`, accessor helpers per parameter
- `input.h/cpp`: `ACT_MATH_TOGGLE` action; default bound to M key
- `main.cpp`: `mathPushFrame()` called once per frame with current Params; `apply_action` handles `ACT_MATH_TOGGLE` → `S.ov.toggleMath()`

## Customizing

Want a parameter you don't see in the readout? Add a row to the `rows[]` array in `drawMathPanel()` plus an accessor function in the anonymous namespace at the top of `overlay.cpp`. New entry in `MathSample` if it's a parameter not already captured (it captures 15 floats — most of `Params`).

Want different thresholds for the regime classifier? Edit the if-else cascade in `drawMathPanel()`.

Want a different math for ρ or D? The estimates are heuristic — refining them against actual measured dynamics is a fun research project. PRs welcome.
