# Research Credits and Source Cross-Reference

This software is a real-time GPU implementation of video feedback dynamics, drawing on five decades of work across nonlinear dynamics, computer graphics, and image processing. This document cross-references every algorithmic decision in the codebase to its primary source.

If you use or adapt this software for academic work, please cite the original sources below — not this implementation. The code here is a synthesis; the ideas are theirs.


## Primary scholarly basis

### Crutchfield (1984): the foundational paper

> Crutchfield, J. P. (1984). "Space-time dynamics in video feedback." *Physica D: Nonlinear Phenomena*, 10(D), 229–245.
>
> https://csc.ucdavis.edu/~cmg/papers/Crutchfield.PhysicaD1984.pdf

This paper defines the entire conceptual framework. Crutchfield writes down the discrete-time iterated functional equation model (his eq. 4) and the continuous reaction-diffusion form (his eq. 7), enumerates the eight to ten observable controls on a physical camera-monitor rig (his Table I), and catalogues the qualitative dynamics into fixed points, limit cycles, chaotic attractors, and "quasi-attractors" with spatial dislocations (his Table II). Section 5 ("Variations on a fight theme") proposes six extensions that motivate much of what we built beyond the baseline.

Direct mappings between Crutchfield's controls and our parameters:

| Crutchfield (1984) Table I | Our parameter (location) |
|---|---|
| zoom (`b`) | `Params::zoom` (main.cpp), `uZoom` (warp.glsl) |
| rotation (`φ`) | `Params::theta` (main.cpp), `uTheta` (warp.glsl) |
| translation | `Params::transX/Y` (main.cpp), `uTransX/Y` (warp.glsl) |
| focus / spatial diffusion | `Params::blurX/Y/blurAngle` (main.cpp), in `optics.glsl` |
| f/stop, brightness, contrast | folded into `Params::contrast` (`contrast.glsl`) |
| color, hue | `Params::satGain`, `Params::hueRate` (`color.glsl`) |
| storage decay (`L`) | `Params::decay` (`decay.glsl`) |
| diffusion (`a`) — Crutchfield eq. 2 | `Params::blurX/Y` Gaussian kernel (`optics.glsl`) |
| noise floor (Appendix) | `Params::noise` (`noise.glsl`) |
| luminance inversion (`s = -1` in eqs. 1, 3) | `Params::invert` (`physics.glsl`) |
| color-channel cross-coupling — Crutchfield eq. 5 off-diagonal `L̄` | `Params::colorCross` (`physics.glsl`) |
| photoconductor response `i₀ ∝ Iᵢ^γ` (Appendix) | `Params::sensorGamma` (`physics.glsl`) |
| saturation threshold `I_sat` (Appendix) | `Params::satKnee` (`physics.glsl`) |

The four "physics layer" knobs (luminance inversion, sensor gamma, saturation knee, color crosstalk) come directly from Crutchfield's primary model and his appendix. Plates 6 and 7 in his paper, which depict luminance-inverted pinwheels and color waves analogous to the Belousov–Zhabotinsky reaction, are the reference visuals for what these knobs unlock.

The "couple" layer (`couple.glsl`) is motivated by Crutchfield's variations 4 and 6 in section 5, which propose connecting multiple feedback systems and inserting a digital frame buffer to allow "nonlocal" coupling between fields.

The companion 16-minute film by the same author is also worth watching:

> Crutchfield, J. P. (1984). "Dynamics in the space of images." 16-minute videotape, U-matic. Available: https://www.youtube.com/watch?v=B4Kn3djJMCE


### Turing (1952): reaction-diffusion as the continuous-time framing

> Turing, A. M. (1952). "The chemical basis of morphogenesis." *Philosophical Transactions of the Royal Society of London. Series B*, 237(641), 37–72.

Crutchfield explicitly grounds his continuous-time model (eq. 7) in Turing's reaction-diffusion equations. Our preset `03_turing_blobs.ini` is named for this lineage. The pattern formation we get with low decay, modest blur, and slow inward zoom is the video-feedback analog of the morphogenetic patterns Turing describes.


## Coupled map lattices: the multi-field architecture

### Kaneko (1984, 1985): origin of the CML formalism

> Kaneko, K. (1984). "Period-doubling of kink-antikink patterns, quasiperiodicity in antiferro-like structures and spatial intermittency in coupled map lattices — toward a prelude to a field theory of chaos." *Progress of Theoretical Physics*, 72(3), 480–486.

> Kaneko, K. (1985). "Spatial period-doubling in open flow." *Physics Letters A*, 111(7), 321–325.

Our `--fields N` option (1 to 4 coupled feedback fields), the ring topology in the main loop, and the symmetry-breaking sign flips in `warp.glsl` (fields 0,2 use +θ; 1,3 use −θ) and `color.glsl` (fields 0,3 use +hueRate; 1,2 use −hueRate) implement a small coupled-map-lattice. The mirroring is what prevents the ring from collapsing to a trivial synchronized state — without it, all fields would drift toward the same attractor and "coupling" would be a no-op.

The relevant code is in `main.cpp` (the main `for (int f = 0; f < N; f++)` loop that walks the ring, reading from field `(f+1) mod N`) and in `couple.glsl` (the per-pixel blend with the neighbour's previous frame). Our preset `05_kaneko_cml.ini` is the closest thing to a faithful CML regime that this rig can produce.

### Crutchfield & Kaneko (1987): joint phenomenology

> Crutchfield, J. P., & Kaneko, K. (1987). "Phenomenology of spatiotemporal chaos." In *Directions in Chaos* (ed. Hao Bai-Lin), pp. 272–353. World Scientific, Singapore.

This collaboration directly bridges the video-feedback work and the CML work, and is implicitly the motivation for combining them in a single tool.


## Procedural noise: the thermal layer

### Perlin (1985, 2002): foundational noise function

> Perlin, K. (1985). "An image synthesizer." *Computer Graphics (Proceedings of SIGGRAPH 85)*, 19(3), 287–296.
>
> Perlin, K. (2002). "Improving noise." *ACM Transactions on Graphics (Proceedings of SIGGRAPH 2002)*, 21(3), 681–682. https://mrl.cs.nyu.edu/~perlin/paper445.pdf

Our `vnoise2()` function in `thermal.glsl` is technically value noise (the cheaper precursor) rather than full Perlin gradient noise — we trade some statistical quality for shader simplicity. The two-octave summation is the standard fractal-noise pattern Perlin documents. The technique of computing displacement as the gradient of a scalar noise field, and then optionally rotating it 90° to get a divergence-free curl component, is from Perlin's noise toolkit.

Ken Perlin received an Academy Award for Technical Achievement (1997) for this work; it is in regular use in essentially every CGI production pipeline.


### Stable Fluids (1999): the thermal layer's research roadmap

> Stam, J. (1999). "Stable fluids." *Proceedings of SIGGRAPH '99*, 121–128. https://doi.org/10.1145/311535.311548

We do **not** currently implement Stable Fluids — our thermal layer is "Tier 1" (noise-based UV displacement). But Stam's algorithm is the canonical path forward to genuine GPU fluid simulation, and would be the basis for any future "Tier 3" implementation that supported persistent vortex injection and true tornado formation. The semi-Lagrangian advection scheme, Helmholtz-Hodge decomposition, and pressure projection in Stam's paper are the standard building blocks for real-time fluids on the GPU.

Stam's later ports to small devices and his 2020 NVIDIA presentation are also referenced in his publications page: https://www.josstam.com/publications


### Harris (2004): GPU implementation of fluid dynamics

> Harris, M. J. (2004). "Fast fluid dynamics simulation on the GPU." In *GPU Gems*, ch. 38. Addison-Wesley.

The reference recipe for porting Stam's algorithm to fragment shaders. If we ever upgrade `thermal.glsl` from noise-based to full Navier–Stokes, this is the implementation we'd follow.


## Tone mapping and color science

### Reinhard et al. (2002): the saturation knee

> Reinhard, E., Stark, M., Shirley, P., & Ferwerda, J. (2002). "Photographic tone reproduction for digital images." *ACM Transactions on Graphics (Proceedings of SIGGRAPH 2002)*, 21(3), 267–276. https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf

Our `uSatKnee` parameter in `physics.glsl` uses the Reinhard formula `x / (1 + x)` for soft saturation. This is the simplified global form of the operator from this paper. Crutchfield's appendix mentions saturation behaviour qualitatively; Reinhard et al. give the actual mathematical operator we use to model it. The compensating `* 2.0` rescale in our code is so that input value 1.0 maps roughly to output 1.0 at full knee, rather than collapsing to 0.5.

Reinhard's work is rooted in Ansel Adams' Zone System for analog photography; our usage in the context of video feedback is somewhat unusual — we're using a tone-mapping operator inside an iterative loop rather than as a post-process — but the math transfers cleanly.


## File format and engineering choices

### Image quality: 32-bit float framebuffers

The `--precision 32` option allocates RGBA32F textures rather than the default RGBA16F. This matters for long cascades because the decay parameter `L` in Crutchfield's eq. 1 is a multiplier per iteration. With 16-bit half-floats, decay values above ~0.998 become indistinguishable from 1.0 due to quantization, capping useful cascade length. With 32-bit floats, decay values up to 0.99999+ remain meaningfully distinct, allowing the multi-minute and multi-hour transients Crutchfield describes ("one evening this cycle was allowed to oscillate for two hours...", section 4, footnote).

### Shader pipeline structure

The orchestrator pattern in `main.frag` (each layer toggleable, host uploads enable bitmask) is a standard real-time graphics technique with no specific paper credit owed. The `#include` resolution at runtime is a pragmatic engineering choice for live shader editing.


## Inspirations not directly used in code

These works are not implemented but were significant influences on the design of the system as a whole.

### Wolfram (2002): cellular automata as the discrete-time analog

> Wolfram, S. (2002). *A New Kind of Science*. Wolfram Media.

Crutchfield's section 5 explicitly frames video feedback as a continuous-state generalization of cellular automata. Wolfram's notion of computational irreducibility is what makes video feedback interesting as an experimental system — there is no shortcut to knowing what an attractor will look like other than running the simulation.

### Belousov-Zhabotinsky chemistry (referenced via Winfree)

> Winfree, A. T. (1983). "Sudden cardiac death: a problem in topology." *Scientific American*, 248(5), 144–157.

> Winfree, A. T. (1983). "Singular filaments organize chemical waves in three dimensions: parts 1, 2, and 3." *Physica D*, 8, 9.

Crutchfield's Plate 7 explicitly compares his luminance-inverted color waves to the Belousov-Zhabotinsky reaction. The dynamics our `physics.glsl` layer enables, when combined with the right rotation and decay, attempt to reproduce this analogy.


## Implementation acknowledgments

### Library credits

This software depends on:

- **GLFW** (https://www.glfw.org) — windowing and input
- **GLEW** (https://glew.sourceforge.net) — OpenGL extension loading
- **stb_easy_font** by Sean Barrett (https://github.com/nothings/stb) — bitmap text rendering for the HUD
- **FFmpeg** (https://ffmpeg.org) — post-session EXR→MP4 encoding (hevc_nvenc / h264_nvenc with libx265 / libx264 CPU fallback)
- **Media Foundation** (Microsoft) — webcam capture on Windows


## How to cite this software

If you use this implementation in academic work, please cite the underlying papers above (especially Crutchfield 1984) rather than this code. If you specifically need to reference this implementation, the canonical attribution is:

> Real-time GPU video feedback simulator implementing Crutchfield's 1984 model with Kaneko coupled-map-lattice extensions and Perlin noise displacement. Source code: [your repository URL]

Implementations should not be cited where the underlying papers exist.


## Document history

This document was assembled in 2026 by cross-referencing the codebase against the cited primary sources. If you find a missing credit or a misattribution, please report it — accurate attribution is more important than convenience.
