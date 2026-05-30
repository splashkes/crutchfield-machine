# Dynamics as Control Surface — Precedents Brief

**Context.** Crutchfield Machine is a discrete-time GPU video feedback system whose parameter space (decay, blur, coupling, noise, warp, gain) maps non-trivially to qualitative regimes — damped, stable limit cycle, quasi-periodic, edge-of-chaos, turbulent, divergent. Current UI: one slider per coefficient. Question: how do other instruments expose the *dynamics* as the playable layer instead of the raw coefficients?

---

## Audio / DSP precedents

### Mutable Instruments — Plaits, Marbles, Rings

Plaits is the cleanest regime-as-control in modern hardware. A single MODEL knob scrolls through sixteen synthesis engines (VA, waveshaping, FM, granular, modal, particle, string, bass drum). MODEL is the regime control, not an algorithm selector. HARMONICS and TIMBRE are remapped per model but always carry the same role: harmonic spectrum and spectral tilt. MORPH is intentionally undefined — whatever the engine's most expressive degree of freedom is. A small OLED renders the engine's character as an animated glyph; the LED above MODEL shifts colour. You feel the regime change before you understand it.

Marbles is the more relevant one. A random-but-structured CV generator with a key control labelled DEJA VU — a probabilistic loop-locking parameter. At 0% fully stochastic; at 50% it loops with mutations; at 100% deterministic loop. The knob is continuous through a phase transition. SPREAD morphs the output distribution (quantized → gaussian → bimodal); BIAS warps shape. LEDs underneath outputs glow brighter as the system settles into a stable pattern — visual feedback of dynamical state, not parameters.

Rings exposes a STRUCTURE knob walking the eigenmodes of a modal resonator from string-like through membrane to metallic. Émilie Gillet's interviews are explicit: STRUCTURE is the bifurcation parameter of the underlying physical model, surfaced as one morphing knob.

### Madrona Labs — Aalto, Kaivo, Sumu

Randy Jones's UIs animate patch cords with the *signals themselves*: a low-frequency LFO is a slow pulse along the cord, an audio-rate signal blurs into a continuous beam. The patcher becomes a phase portrait at a glance. Aalto's complex oscillator has a MODE knob running sine → FM-injection → chaotic feedback → hard-sync — bifurcation parameter as knob. Kaivo's body model (a 2-D mesh) is drawn live, deforming and ringing as you play. You see the attractor. Sumu renders partial trajectories in 3-D as you sweep. Randy is the precedent for "visualise the state space, not the parameters."

### Make Noise — Maths, René, Mimeophon

Maths is two function generators with rise / fall / cycle. It's a chaos generator only when self-patched: VAR_OUT into RISE or FALL, scaled. The Maths Illustrated Supplement (Rolando & Farrell) diagrams patches by *intent* — "complex modulation source," "self-erasing percussion envelope," "stuttering LFO." That is regime documentation. The knob is identical; the regime is in the patch. Suggests a "regime cookbook" layered over the same coefficients.

René is the canonical cartesian sequencer: a 4×4 grid traversed by multiple state machines. The *direction of traversal* (snake, diagonal, cardinal, random) is the playable parameter, not the values in the cells. Trajectory as control, not point. Mimeophon's main knobs (HALFSPEED, 2X) and zone labels A–F denote temporal regimes the maker tuned by ear, not specced.

### Buchla / Serge, Eurorack chaos modules

The Buchla 265 "Source of Uncertainty" exposes three CV outputs (fluctuating, quantized, stored random) with a knob over the *probable distribution* — a literal PDF control. Serge's SSG exposes smoothness and couple; the couple knob injects a fraction of the smooth output back into the stepped input, putting the device into a coupled-oscillator regime. Mutable Tides has a MODE knob that walks AD → looping AD → AR → cyclic LFO → audio-rate oscillator — bifurcation as menu walk. Nonlinear Circuits' chaos modules (Sloths, Jellyfish) ship with no UI beyond patch points; the manuals are phase-portrait illustrations. Pattern across all: panel art and patch sheets describe *behaviours*, not values.

### SuperCollider chaotic UGens

`HenonC`, `LorenzL`, `StandardL`, `LinCongC` etc. all expose the *equation coefficients* as args. This is the negative example. You cannot tell from the SC docs whether `LorenzL.ar(sr, 10, 28, 2.667)` is in the canonical chaotic Lorenz regime or has been pushed to a stable spiral — you have to know the math, or you scope it and listen. There is no regime label, no distance-to-bifurcation. It is what Crutchfield's current UI is.

### Pure Data / Max — `gen~`

`gen~` lets you write z-domain feedback patches directly; the recipe culture (Graham Wakefield tutorials, Surreal Machines) is the "regime cookbook" pattern. No UI for distance to instability — patches are labelled "stable comb," "self-oscillating reverb," "blow-up if you push delay below 4 samples." Folk knowledge of bifurcation surfaces.

### TouchDesigner

TD has Math CHOPs and Analyze CHOPs but the relevant precedent is the **Feedback TOP** wired to a Composite + Transform + Blur, which is structurally identical to Crutchfield's pipeline. The community-standard practice (Matthew Ragan, Bileam Tschepe / elekktronaut tutorials) is to expose feedback gain as a **single damped slider with a visible "instability marker"** — a screenshot of the histogram or a numeric "max luma" readout next to the slider, so the operator can see how close they are to runaway. That is the closest existing UI pattern to what Crutchfield wants: a *parameter slider with a live dynamical-state readout adjacent*.

---

## Visual / generative precedents

### Notch Builder, VDMX

Notch's "Modifier" system wraps any property in a curve UI that maps a driving signal (audio, time, noise) to that property. Modifiers stack and the resulting effective curve is rendered live — you edit the dynamics of the parameter, not the parameter. VDMX (David Lublin) makes every internal signal (FFT bands, MIDI, OSC) first-class with a live waveform readout; the metaphor is "what's the shape of this control signal right now" not "what's its current value."

### Hydra (Olivia Jack)

Hydra is a livecoded video feedback system, so the direct cousin. The relevant idiom is the use of `.modulate()`, `.scale()`, `.kaleid()` as chained transforms where the *coefficients are typed as numbers* but the community-shared sketches always come with a **regime caption**: "this one is a strange-attractor look," "this one is a Reaction-Diffusion-like spot pattern," "drift this number above 1.2 and it explodes." The cookbook is the regime layer. Like `gen~`, it's folk knowledge organised by behaviour.

### Tidal Cycles / Strudel

Tidal's `degradeBy`, `sometimes`, `chunk`, `jux rev` are not parameter knobs. They are *pattern transforms* — operators on the trajectory of events, not on the events. This is the strongest precedent in pattern-land for "control the dynamics, not the points." A Tidal performer scrolls through transforms, not values.

### Processing / p5.js

Daniel Shiffman's *Nature of Code* visualises Lorenz, Henon etc. with sliders for `a, b, c` and a phase-portrait window beside them. Canonical pedagogical pattern: parameter + portrait, side by side.

---

## Mathematical precedents

### Phase-portrait UIs

Mathematica's `StreamPlot[]` and `Manipulate[]` combo, Desmos's slider-driven phase plots, Maxima's `plotdf`, and academic tools like XPPaut (Bard Ermentrout) all share one pattern: **a parameter slider next to a live phase portrait, with bifurcation curves overlaid on the parameter axis itself**. XPPaut is the most explicit — it computes the locus of Hopf and saddle-node bifurcations in parameter space and *draws them on the slider*. That is the literal "distance to bifurcation" UI Sean asked about.

### Academic music research

Agostino Di Scipio's *Audible Ecosystemics* pieces and Curtis Roads's writing on chaotic synthesis both argue for Lyapunov-exponent-based control. The proposed UI is consistently the same: a real-time computed Lyapunov estimate displayed as a number or coloured bar next to the parameter, with a threshold marker for the zero crossing (the chaos boundary). Implementations are mostly research prototypes; no consumer instrument ships this.

---

## The five that translate

1. **XPPaut / phase-portrait tools** — the literal model. A slider with bifurcation thresholds drawn *on the slider itself*, plus a small phase portrait beside it.
2. **Marbles DEJA VU** — single continuous knob through a phase transition, with LED brightness as live state readout. Translatable to a "feedback coherence" master knob.
3. **TouchDesigner feedback-loop conventions** — parameter slider with adjacent live histogram / max-luma readout. The lowest-effort, highest-leverage borrow.
4. **Plaits MODEL knob + Mimeophon zones** — a labelled regime selector that is independent of the underlying coefficients. The coefficients get remapped per regime; the operator plays the regime.
5. **Tidal Cycles pattern transforms** — operators on the trajectory rather than the state. Translatable to "apply transform X to the current dynamics" rather than "set parameters to Y."

## Metaphors that work

Four show up repeatedly across the precedents that respect dynamics:

- **The regime label.** A named zone (Plaits engine names, Mimeophon A–F, Marbles deja vu percentages). The label is the contract: "you are now in turbulent." The coefficients underneath are free to remap.
- **Distance markers on the parameter itself.** XPPaut bifurcation curves drawn on the slider, TouchDesigner instability markers, Maths patch sheets. The parameter axis is annotated with where the qualitative changes happen.
- **Live state visualisation adjacent to the control.** Madrona Labs' animated cords, Marbles' settle LEDs, Aalto's MODE knob with live oscilloscope. Not separate, not behind a tab — *next to* the knob.
- **The trajectory operator.** Tidal transforms, Maths self-patching, René traversal selection. You don't edit the point; you edit the path through state space.

## What all of them do that Crutchfield's current parameter editor doesn't

Every precedent above shares one structural property the current UI lacks: **the parameter is never the only thing on screen at the moment of control.** There is always either (a) a regime label, (b) a state readout, (c) a phase-portrait or trajectory glyph, or (d) a bifurcation marker — adjacent, live, and salient. The current Crutchfield UI is a column of unannotated sliders. The operator is asked to maintain a mental model of the dynamics while flying the coefficients. None of the precedents do this.

A second shared property: **the regime is a named, navigable, first-class object.** It is not derived in the operator's head from parameter readings. Plaits names sixteen of them. Mimeophon names six. Marbles names a continuous DEJA VU axis. Hydra and Tidal name them in shared sketches. Crutchfield has names in its philosophy doc (damped, edge-of-chaos, turbulent, divergent) but not in the UI.

## Recommendations for a video-feedback context

Three concrete moves, ordered by leverage-to-effort.

**1. Annotate the existing sliders with bifurcation markers and a live regime label.** Cheapest possible move. Run a small live estimator off the framebuffer (luma variance, frame-difference energy, dominant spatial frequency) and classify into damped / stable / edge / turbulent / divergent. Render the classification as a coloured tag at the top of the editor. On each slider that meaningfully moves the regime (decay, coupling, noise gain), draw the threshold tick where the classifier flips. This is the TouchDesigner + XPPaut hybrid and it can ship in a sprint without touching the underlying dynamical model.

**2. A single MODE / REGIME knob as the master.** Plaits-style. Continuous, scrolls through pre-curated regime presets, each preset is a multivariate setting of the underlying coefficients. Crossfade between adjacent regimes. The current parameter sliders become detail editors that the operator dives into only when they want to deviate from the curated path. This is the "regime as first-class control surface" move and it changes the instrument's character — you are no longer mixing coefficients, you are flying through regime space.

**3. A live phase-portrait inset.** Two-dimensional, plotted in (mean luma, frame-difference energy) or (spatial variance, temporal variance), trailing 2–4 seconds of history. This is the Madrona Labs / XPPaut / Nature-of-Code move. Side-effect: the operator learns the geometry of the instrument by watching the trail loop, settle, smear, or escape. That visual is the *single best teacher* the system can have.

Combined, these three give the operator a regime label, a regime knob, and a phase portrait — the three things every precedent above provides and the current editor doesn't.

---

*Refs: Gillet interviews (Sonic State, 2017–21); madronalabs.com/journal; Maths Illustrated Supplement (Rolando & Farrell, 2013); Bileam Tschepe TD tutorials; Hydra docs; McLean, Tidal paper (2014); Ermentrout, XPPaut docs; Di Scipio, "Sound is the interface" (Organised Sound, 2003); Shiffman, Nature of Code (2012).*
