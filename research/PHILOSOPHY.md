# PHILOSOPHY

This document is the back-story and philosophical spine of the project.
For citation-level academic attribution, see `CREDITS.md`. For a factual
parameter/control reference, see `README.md`. This file is about *why*.

## Where this started

The project began with a question, not a specification. From Simon's first
message:

> I have spent hundreds more likely thousands of hours plugging RCA video
> cables into different boxes back to other boxes video mixers (which is a
> very direct feedback back loop) and also with cameras, overlaid on screens
> or walls or objects. My favourite approaches include changing the camera
> or projector settings such that it is modifying the hue and the feedback
> loop causes it to go through a rainbow... the most interesting ones to me
> were the effects that worked through a sequence — start a cascade trigger,
> let's say alternating black-and-white bars, but because of the saturation,
> just leaving it alone, no setting changes... it goes through a whole
> sequence, and it comes back to the steady state. Some run through
> sequences from steady state back to steady state; some produce a sequence
> of as much as 45 minutes. So within the initial state and processing
> machine, that is the total system, a huge amount of information was
> contained within the initial conditions.

That last sentence is the seed of everything. The project exists to
investigate, embody, and extend that observation — to build an instrument
good enough to reproduce what Simon did with physical rigs in the 80s and
90s, and to find out whether modern GPUs and modern precision can extend
those cascades past what analog ever could.

## The deep point, slightly inverted

Simon's framing — "a huge amount of information in the initial conditions"
— is exactly right phenomenologically but slightly the other way around
mathematically. **The information lives in the operator T, not in the
initial state.**

A video feedback rig is a discrete dynamical system: `I_{n+1} = T(I_n)`,
where `T` is everything the loop does to an image in one pass — the
geometric warp from the camera's pose, the blur from the lens defocus, the
nonlinear response of the camera electronics and the monitor phosphor, the
color shifts introduced by the mixer, the bleed toward black from signal
decay. `T` is the machine. The initial state `I_0` is just a seed: it
selects which trajectory through the enormous phase space you follow, but
it doesn't contain the trajectory. The 45-minute film isn't stored in the
black-and-white bars you started with; it's produced, frame by frame, by
iterating `T`.

This is what Wolfram calls **computational irreducibility**: for systems
of this class, there is no shortcut shorter than running the computation.
The only way to know what the cascade does is to watch it. And what that
cascade reveals isn't information already present in the seed — it's the
latent structure of `T` being unfolded by repeated application.

That distinction matters because it reframes what the instrument is *for*.
You're not engineering initial conditions to produce outcomes. You're
shaping `T` — adjusting its parameters, enabling and disabling its layers
— so that the space of possible cascades it can produce becomes
interesting. A good `T` has long transients, multiple basins of
attraction, strange attractors with rich but non-repeating structure. A
bad `T` collapses to black or to saturation in a few seconds regardless of
how you seed it. The work, both in physical rigs and in this project, is
tuning `T` into regimes where it has something to say.

## Scholarly lineage

Several people have already thought carefully about systems of this kind.
The most important is **James Crutchfield**, who in 1984 published *Space-
Time Dynamics in Video Feedback* in *Physica D* — a paper that sat down in
a dark room with cameras, monitors, and a mixer (same as Simon) and
produced two mathematical models of what happens. The first is a
discrete-time iterated functional equation `I_{n+1} = T(I_n)`, which is
exactly what this code implements. The second is a reaction-diffusion
partial differential equation in the style of Turing's 1952 *Chemical
Basis of Morphogenesis*, where lens blur acts as the diffusion term and
nonlinear electronic response acts as the reaction term. Both
formulations describe the same object; neither is complete alone.

**Kunihiko Kaneko**'s work on coupled map lattices (starting 1984, same
year as Crutchfield) provides the formal framework for "iterated local
rule with spatial coupling," which is what every video feedback rig is,
and which is what this project's optional multi-field mode makes explicit.

**Christopher Langton**'s edge-of-chaos framework (1990) captures why
Simon's favorite regimes feel the way they do: systems poised between
order and chaos store and transmit maximum information, which is what
long, non-repeating but non-random cascades are.

**Alan Turing**'s reaction-diffusion is the original mathematical story of
how simple local rules with diffusion produce spatial patterns
(spots, stripes, spirals). Video feedback and Gray-Scott simulations are
visual cousins for this reason.

**Steina and Woody Vasulka** are the artists who took this seriously as an
art form from the 1970s onward, and it's telling that Crutchfield's paper
lives permanently in the Vasulka archive — scientist and artists treated
each other as co-investigators of the same object. This project is in
that same lineage.

`CREDITS.md` traces every specific algorithmic choice back to its source.

## Why these design choices

Several architectural decisions came out of first principles applied to
Simon's original observation. Recording them here because they are not
obvious and they define the character of the instrument.

**Fragment shaders, not compute.** The core operation in every layer is a
per-pixel transform with bilinear texture lookups. That's exactly what the
fixed-function texture unit on a GPU is built for. Moving to CUDA or
compute shaders means giving up hardware bilinear filtering and
hardware border handling for no gain — on an RTX 3090, the fragment path
is memory-bandwidth bound long before it's compute bound, so CUDA buys
nothing at this scale. Where compute starts earning its keep is non-local
operations (FFT-based spectral feedback, ML-in-the-loop, custom
histograms, particle coupling). Those are real and interesting but they
are future work, not table stakes.

**Layers as separate files.** The operator `T` is composed of ten or so
distinct operations (warp, optics, gamma, color, contrast, decay, noise,
couple, external, inject, plus physics and thermal added later). These
could all live in one shader, but then changing the math of any one of
them requires touching a monolithic file, and reasoning about what each
does requires disentangling it from the others. Instead, each layer is
its own `.glsl` file defining a single function, and an orchestrator
shader includes them in order behind `if (U.enable & L_X)` guards. The
host uploads an enable bitmask. This means:

  - You can toggle any layer on/off at runtime (F1–F12), live, without
    recompilation, to feel what each one contributes.
  - You can hot-reload shaders with `\`` while the app runs and watch the
    change take effect in the current loop — this is the modern
    equivalent of reaching over and tweaking a knob on a physical mixer.
  - Adding an 11th layer is ~50 lines in one new file plus a single line
    in the orchestrator and a uniform declaration. No existing code
    changes.

The layered design is the single most important architectural choice in
the project. It makes the instrument *hackable in the way a physical rig
was hackable* — you can reach into any part of the signal chain and
modify it without taking the rest apart.

**Four coupled fields with mirrored symmetry breaking.** When the couple
layer is enabled, the system runs not one but up to four simultaneous
feedback fields, coupled in a ring topology (each field reads from its
neighbors through the couple layer). The fields are mirrored: field 1
uses `theta` and `hueRate` as given, field 2 uses `-theta` and
`-hueRate`, etc. This is Kaneko's coupled map lattice regime, and it is
where cascades become genuinely longer than single-field dynamics allow
— a 45-minute cascade in a single field can become an hours-long
non-repeating cascade in a 4-field coupled system. The mirroring
prevents trivial convergence to a single shared state.

**Physics layer separate from gamma layer.** The gamma layer handles the
CRT phosphor's nonlinearity — a ~2.2 gamma curve applied before and
inverted after the main color work. The physics layer (added later,
faithful to Crutchfield's paper) handles the *camera-side* nonlinearities:
luminance inversion (his Plates 6 and 7), sensor gamma, soft saturation
knee, RGB cross-coupling. These are different physical phenomena happening
at different points in the signal chain. Collapsing them into one layer
would be mathematically equivalent but would lose the ability to toggle
them independently and to reason about which part of the analog pipeline
each corresponds to.

**Thermal layer between warp and optics.** The thermal layer applies a
noise-driven UV displacement — heat shimmer to vortex. It's placed
between warp and optics in the pipeline because in the physical world it
*is* between them: it's the air between the camera and the monitor. The
camera looks through this air, so what it sees has already been distorted
by temperature gradients before the lens even focuses. Getting the order
right in the shader pipeline matters because function composition isn't
commutative — applying turbulence after blur is a different physical
system than applying it before.

**RGBA32F internal precision, optional.** The default is RGBA16F (half-
float), which is plenty for visual work. RGBA32F is available via the
`--precision 32` flag and doubles the memory bandwidth cost but lets you
resolve extremely slow decays. At 16F you can't distinguish decay=0.9995
from decay=0.9998 because the LSB gets quantized; at 32F you can, and
that distinction is the difference between a 30-second cascade and a
five-minute one. The observation that "you cannot resolve the very slow
cascades in half-precision" is Simon's contribution; the fix is one
line.

**Lossless EXR sequence recording, not direct MP4.** Recording must not
degrade the live experience. The earliest recording implementation piped
to ffmpeg NVENC directly, which meant the render thread paid the cost of
float→uint8 conversion during `glReadPixels` and then the cost of
blocking on ffmpeg's pipe when the encoder backed up. Both were real
costs that showed up as dropped live fps during recording. The fix:
dump half-float RGBA frames to an EXR sequence with zero render-thread
cost (two GL commands per captured frame, decoupled from disk via a
writer thread with its own GL context and a bounded queue), and let the
user encode to MP4 offline after the session ends. The instrument is not
allowed to sacrifice what it's showing you in order to preserve a record
of it.

## The character of the collaboration

A note about how this was actually built.

The project went through many iterations. Most of the good decisions came
out of pushback, not from the initial plan. Specifically:

  - The single-file prototype became a ten-layer modular build because
    simpler iteration wasn't enough for what Simon wanted to explore.
  - The fine step sizes (zoom 50× finer than the first version, rotation
    similarly) came from Simon observing that the first version's step
    sizes "cascade in a couple seconds" — too coarse to hold a regime
    steady.
  - The HUD overlay was initially inside the feedback loop itself (so it
    fed back into the simulation). Simon caught this; the fix was to
    draw the overlay after the feedback pass.
  - The sleep-based fps cap was Simon's correct observation that "modern
    renderers don't sleep; that's not how video works." Removed entirely.
  - The MP4-encoding-in-loop architecture forced precision loss and
    pipeline stalls. Simon pushed back: "the recording must not degrade
    the live experience." This led to the EXR-sequence redesign.
  - The "downsample recording" idea surfaced briefly as an optimization
    and was correctly killed with "why are we downsampling anything at
    all?" — recording should capture exactly what the live experience is.

The pattern is consistent: the best design decisions came from Simon
refusing to accept implementations that would compromise the live
experience, the precision, or the directness of the signal chain. This
document records that, because the reason this instrument works is not
that it's cleverly engineered — it's that every engineering choice was
forced to respect what the artist actually needed.

## What this instrument is for

Three uses, in rough order of ambition:

**1. A faithful modern re-creation of what analog video feedback rigs
could do.** Physical rigs had limits: 8-bit signal chains, fixed optics,
one loop at a time, no way to save a regime. This project removes all of
those while preserving the character. A user who spent a thousand hours
with RCA cables should be able to sit down at this and feel at home
immediately — the knobs are the same knobs. The starter presets
(`01_default`, `02_rainbow_spiral`, `03_turing_blobs`,
`04_long_cascade`, `05_kaneko_cml`) are meant to be instantly recognizable
as things you could have produced on an SVHS mixer, with the
correspondence made explicit in their names.

**2. A probe for "what was analog video actually doing that we haven't
named yet?"** Some of the physical rig's dynamics came from things that
weren't in any theoretical model — interlace artifacts, thermal noise
floor, raster timing, signal-chain nonlinearity, phosphor persistence.
Some of these we've implemented (the physics layer encodes several); some
we haven't (raster scan effects, proper CRT phosphor persistence with
spatial anisotropy). When a user notices "this regime looks *almost*
right but something's off compared to what I remember," that's a clue
about what physical phenomenon is still missing from `T`. The project is
structured so that adding a new layer to investigate such a clue is
small.

**3. A platform to extend what analog could ever do.** Four coupled
fields at 4K with RGBA32F precision running at 150fps is a regime no
physical rig ever reached. Cascades that lasted 45 minutes on an SVHS
mixer might last hours in this configuration without visible repetition.
The computationally irreducible space of `T`'s behaviors is much larger
than what the analog world got to sample. The instrument is built to
explore that larger space.

## The philosophical throughline

Your physical rig was a hand-made analog computer for the question:
*how much behavior can a simple rule unfold?* Turing asked it, Wolfram
asked it, Kauffman asked it, Crutchfield asked it with cameras. This
project is one more way of asking, with modern precision and modern
speed, and with the explicit hope that seeing the answer at 4K with
RGBA32F across four coupled fields for hours at a time will reveal
something those earlier implementations couldn't show.

The instrument is not the work. The instrument is the lens. The work is
what you see when you look through it.
