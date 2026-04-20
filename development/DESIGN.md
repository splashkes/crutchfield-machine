# DESIGN — what Crutchfield Machine is, and why

This doc describes *intent*. For the *mechanism*, see [ARCHITECTURE.md](ARCHITECTURE.md).
For specific technical decisions, see [ADR/](ADR/README.md).

## In one sentence

A real video feedback system — full-precision, live-editable, lossless-
archivable — faithful to the analog-era work of the Vasulkas, James
Crutchfield, and the early dynamical-systems community.

## What this is

**A dynamical system with a rendering front-end.** The core is a
per-frame iterated function on a floating-point 2D field. Everything
else — the UI, the recorder, the camera input, the overlay — exists to
serve that core.

**A faithful digital descendant of analog video feedback.** The
Vasulkas built feedback rigs from cameras and CRTs; Crutchfield's 1984
paper formalised what those rigs were doing mathematically. This
project takes those papers as a starting point and gives them a
compute budget they never had.

**A composable shader stack.** Twelve layers (warp, optics, gamma,
color, contrast, decay, noise, coupling, external, inject, physics,
thermal) in plain `.glsl` files under `shaders/layers/`. Each layer is
a single function in 5–40 lines. Hot-reloadable with `\`.

**A working medium.** Presets are INI files. Shaders are editable.
Every parameter is on a key. Everything the system does is visible and
hackable. There's no black box.

## What this is NOT

- **Not a sequencer or DAW.** We don't make music. We can follow an
  external music source (Strudel via MIDI, planned) and ride its
  tempo + events, but the composition lives in the other tool.
- **Not a fractal generator.** The mathematics are *continuous
  dynamical systems*, not iterated escape-time renderers.
- **Not a filter plug-in.** It's a standalone application with its own
  feedback state, not something you drop on footage in post.
- **Not maximising visual "wow" at the expense of fidelity.** If we
  have to choose between a prettier rendering hack and a physically
  meaningful implementation of what the papers describe, the papers
  win.
- **Not cross-platform-first.** Windows is the reference build. Linux
  and macOS ports exist as stubs; contributors welcome (see
  `linux/README.md`, `macOS/README.md`).

### Performance use has moved from "not a goal" to "a first-class
### use case"

Earlier revisions of this doc said "Not a VJ/performance tool. No
MIDI yet, no sync, no beat detection." That's no longer accurate:

- BPM sync + tap tempo with beat-locked visual modulations are in.
  (See ADR-0008.)
- Gamepad mapping is contextual per help section, so the whole
  controller surface drives whichever parameter group the user has
  focus on. (See ADR-0009.)
- Edirol V-4-inspired effect slots cover 18 effects with a single
  CONTROL parameter each — the live-switcher vocabulary of the
  V-4 brought into the feedback system.
- Real MIDI input (Strudel / hardware controllers) is the next
  pending piece. Plan in `development/plans/strudel_midi_sync.md`.

The "research-grade, not research-only" principle below already
permitted this — we were always going to want the system to be
pleasurable to perform with. The infrastructure just caught up.

## Principles

### Precision over prettification
Every internal operation runs in float unless explicitly opted out
(`--precision 8` for studying quantization effects). We don't clamp to
8-bit anywhere in the default feedback path. This is load-bearing —
see [ADR-0001](ADR/0001-rgba32f-default-precision.md).

### Directness
A user should be one keystroke away from any parameter, one shader
reload away from a code change, one `Ctrl+S` away from a saved preset,
one `bindings.ini` edit away from any rebinding they want. No hidden
modes, no wizards. The drill-down help panel (H) shows every action,
its current value, and the key/pad/MIDI bindings driving it.

### Extensibility through the shader layer system
New visual behaviors should be one new `.glsl` file, one `#include`
line, one uniform, one key binding. If adding a feature requires
architectural surgery, that's a signal the architecture needs
reconsidering — not that the feature needs to be shoehorned in.
See [CONTRIBUTING.md](../CONTRIBUTING.md) for the authoring walkthrough.

### Lossless when archival matters
The recorder writes half-float EXR sequences *directly from the
simulation FBO*, not the display framebuffer. What gets captured is
what the math produced, bit-exact. This is what separates a dynamical-
systems research tool from a screen-recorder. See
[ADR-0002](ADR/0002-half-float-exr-recording.md).

### No compromise on the build binary
The distribution is one statically-linked .exe plus shaders. No
installer, no runtime dependencies, no "also install this DLL pack".
A cold user on a stock Windows machine runs it on first try.
See [ADR-0003](ADR/0003-static-linking-for-distribution.md).

### Research-grade, not research-only
The papers are in `research/` and the architecture respects them, but
the system has to be *pleasurable to run*. If a preset takes 30
seconds to show anything, it's a bad preset. If the default launch
behavior is a black screen, that's a UX bug we should fix.

## Target users

In order of priority — the top audience shapes defaults.

### 1. Installation artists and gallery technicians
Long-running unattended mode, 4K output, zero-drop recording, predictable
startup. This is who `--demo`, `--fullscreen`, and the statically-linked
binary are for.

### 2. Dynamical-systems researchers
People who have read Crutchfield's paper, who care that decay is
multiplicative not additive, who want to record the *state*, not just
the display. This is who the `--precision 8` vs `32` comparison,
`--iters`, and EXR recording are for.

### 3. Shader hackers and creative coders
People who want to add their own layer and have it running in 60 seconds.
This is who `CONTRIBUTING.md`, the `.glsl` layer system, and live-reload
are for.

### 4. Video art audiences
People encountering the output in a gallery, online, or on someone
else's screen. They don't interact with the app directly but their
experience shapes whether the above three groups have anything to
show off. This is who the curated presets and gallery screenshots are for.

## Aesthetic direction

### What we aim for
- **Emergence.** Structures that form from rules, not hand-drawn
  figures. Spiral arms, cellular patterns, self-sustaining orbits.
- **Saturation without blowout.** Rich color throughout the signal
  range, not crushed highlights or muddy midtones. Part of why
  precision matters.
- **Stable unpredictability.** Attractors that don't collapse to
  trivial fixed points (black, white, single color) and don't diverge
  to hash. The edge of chaos in the Langton sense.
- **Variety across time.** A good preset evolves. Looking at the same
  preset for five minutes should show multiple regimes, not one
  static pattern.

### What we avoid
- **Filter-stack aesthetic.** Photoshop-style effects pasted together
  — there's nothing wrong with that but it's not what this is for.
- **Cheap chaos.** High-frequency noise masquerading as complexity.
- **Fractal-escape-time aesthetics.** Mandelbrot-adjacent imagery.
  Different mathematical space.
- **Demo-scene pyrotechnics.** 3D tunnels, particle fountains.

## Non-goals (things we've considered and declined)

- **AI/ML postprocessing.** A neural upscaler or stylization pass would
  contradict the "what you see is what the math produced" principle.
- **Node-graph editing.** The `.glsl` layer system is the editing
  surface. A visual node editor would add a layer of indirection
  without adding capability.
- **Built-in video codec encoding.** We write EXR sequences and offer
  an `ffmpeg` hand-off. Integrating libav would double the binary size
  and lock us to a specific codec era.

## How to tell when a feature belongs

Ask: *does this serve one of the target-user groups, and does it
respect the principles above?*

- Audio reactivity → yes (installation + creative coders, principle:
  extensibility). On roadmap.
- MIDI input → yes (performance). Infrastructure is in, real impl
  pending — see `development/plans/strudel_midi_sync.md`.
- A/B bus V-4 refactor → yes but expensive. Held back with a written
  plan in `research/edirol_v4_ab_bus_future.md`; revisit when core
  is stable.
- A Photoshop-style filter pack → no (aesthetic direction).
- A built-in video editor → no (non-goal).
- A preset marketplace → maybe, but not a priority over getting the
  core system right.

When it's unclear, write an ADR proposing the feature and see if the
argument holds up on paper.
