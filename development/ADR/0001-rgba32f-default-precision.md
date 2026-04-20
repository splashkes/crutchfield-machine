# ADR-0001 — RGBA32F feedback buffer as default precision

**Status:** Accepted
**Date:** 2026-04-19 (retroactive — formalising a decision that shaped the project from day one)
**Retroactive:** yes

## Context

Video feedback dynamics depend heavily on the precision of intermediate
state. Each frame multiplies by decay, adds noise, applies contrast,
and feeds back — small quantization errors compound quickly. Analog
feedback rigs are effectively infinite-precision; 8-bit digital
equivalents produce visibly coarser attractors with dead zones and
banding where analog versions evolve continuously.

Available options for the ping-pong FBO format on modern GL:

- `GL_RGBA8` — 4 bytes/pixel, 256 levels/channel, [0,1] clamp.
- `GL_RGBA16F` — 8 bytes/pixel, ~11-bit effective precision, unbounded range.
- `GL_RGBA32F` — 16 bytes/pixel, 24-bit precision, unbounded range.

Performance on a modern dGPU: 32F is ~1.5-2× the memory bandwidth of
16F at 4K but is not the bottleneck on RTX 3090-class hardware.

## Decision

Default to `GL_RGBA32F` for the feedback FBO. Expose `--precision 16`
as a performance option for weaker GPUs and `--precision 8` as a
studies option for comparing quantized dynamics against the float path.

## Consequences

**Positive:**
- The dynamics that emerge match the mathematical description in the
  Crutchfield 1984 paper without being distorted by quantization.
- Color information at low magnitudes (e.g. late-stage decay) survives
  through many iterations instead of clamping to zero prematurely.
- `--precision 8` becomes a research/demo feature: users can directly
  see what 8-bit quantization does to the attractor.

**Negative:**
- Memory bandwidth cost. On an RTX 3090 at 4K60 this is fine; on older
  hardware users must manually opt down to 16.
- Recorder has to convert float→half during readback (done in the
  reader threads, CPU-side).

## Alternatives considered

- **Default to RGBA16F.** Saves bandwidth, visually near-identical for
  most presets. Rejected because some late-decay and coupled-field
  regimes show quantization at 16F that 32F avoids; having the default
  be the "faithful" choice matches the project's stated priorities.
- **Let the user pick based on GPU detection.** Too clever; the system
  should be predictable. Document the option and let users tune.

## References

- `main.cpp: resize_fbo()` — internalFormat switch on `g_cfg.precision`.
- `research/Crutchfield_1984_Vasulka.pdf` §2 — video physics.
- DESIGN.md — "Precision over prettification" principle.
