# ADR-0017 — Pixelate takes over the sample stage; optics is bypassed when active

**Status:** Accepted
**Date:** 2026-04-20
**Retroactive:** no

## Context

The pixelate layer quantizes feedback onto a screen-space cell grid
(dots / hard squares / rounded squares × s/m/l). Three designs were
on the table for how it should interact with the existing pipeline:

1. **Post-sample overwrite.** Run after all other layers, sample
   `uPrev` at cell centres, overwrite the final `col` of each pixel
   with the per-cell sample. This was the first attempt.
2. **UV-space modifier before optics.** Quantize `src_uv` to cell
   centres, then let `optics_sample` do its thing at the quantized
   UV.
3. **Alternate sample stage.** When pixelate is active, *replace*
   `optics_sample` entirely. Pixelate performs the uPrev read (one
   sample per cell, at the cell centre's warp+thermal-transformed
   UV) and returns the result. Optics is skipped for that frame.

(1) and (2) both failed in different ways:

- (1) broke propagation. Cell centres sampled the **raw** previous
  frame — which meant decay, coupling, noise, inject, and every
  other layer's work downstream of pixelate was silently discarded
  each frame. User-visible symptom: "pixelate takes over, nothing
  else propagates."
- (2) produced soft cells. Optics' 5/9/25-tap blur samples the
  neighbourhood of `src_uv`; pixels within a cell would pick up
  slightly different weighted averages of the same cell centre plus
  boundary spillage. Hard pixel edges — the whole point of
  pixelation — dissolved.

## Decision

Adopt (3): pixelate is a **mutually exclusive alternate sample
stage**. When `uPixelateStyle != 0`, `main.frag` calls
`pixelate_apply(uPrev, src_uv, uv)` and **skips** `optics_sample`.
When `uPixelateStyle == 0`, the pipeline falls through to
`optics_sample` as before. Both paths return a `vec4` that flows
into the same downstream chain (invert → physics → gamma → color →
contrast → gamma → decay → couple → external → inject → noise → vfx
→ output_fade).

`pixelate_apply` internally:

1. Computes the cell that the current pixel lies in (screen-space
   `uv` / `cellUV`).
2. Re-applies `warp_apply` and `thermal_warp` to the cell **centre**
   to get the cell's sample position `cell_src`. Every pixel in a
   given cell therefore samples `uPrev` at the same texel, and that
   texel moves with the warp/thermal dynamics — which is what makes
   pixelated content propagate across frames.
3. Samples `uPrev` once at `cell_src`. For "hard squares" every
   pixel in the cell returns this block value. For "dots" and
   "rounded" a per-shape mask blends the block sample against a
   second per-pixel sample at `src_uv`, so the gap pixels continue
   to evolve at full resolution between the quantized shapes.
4. Applies the CRT-bleed preset (jitter, chromatic offset, edge
   softening, vignette, burn — each preset emphasizes a different
   aspect; see `pixelate_decode_bleed`).

## Consequences

### Wins

- Pixelated output **feeds back correctly**: the next frame's warp
  reads from the written blocky state, the pipeline's work
  (decay, coupling, noise, injections) applies on top of the
  sampled block value, and the result re-lands on the grid next
  frame. Blocky attractors evolve through the dynamics instead of
  freezing.
- Pixel edges stay **hard** because cells don't overlap each other's
  sample neighbourhoods via a blur kernel.
- Design symmetry: both sample options return `vec4`; both take the
  warped `src_uv`. Callers use a clean if/else.

### Costs

- Optics blur + pixelate is not available simultaneously. Users who
  want "soft pixel" look have to use the "dots" / "rounded" variants
  or the "soft" CRT bleed preset, not the blur layer.
- Cell-centre re-application of warp and thermal is a cost multiplier
  (the transformation runs twice per pixel — once for the original
  `src_uv`, once for `cell_src`). Both operations are cheap
  arithmetic; negligible at 4K on the target GPU.
- An extra per-pixel conditional in the shader's sample dispatch.
  Branch coherence is high (all pixels take the same branch per
  frame), so this is essentially free on modern hardware.

## Alternatives considered

### Post-sample overwrite (rejected)

The original attempt. Failed because cell samples came from the
un-processed previous frame, skipping every downstream layer. Fix
would have required storing the processed frame in a second texture
and sampling THAT at cell centres — effectively inverting where
pixelate sits in the pipeline, at which point design (3) is cleaner.

### UV-space quantizer before optics (rejected)

Quantize `src_uv` and let optics sample at the quantized UV. Soft
cells because optics' kernel averages across the quantized boundary.
Would need special-casing optics to use nearest-sample when pixelate
is active, at which point we're back to design (3) with more
branching.

### Pixelate in two stages (hybrid)

Sample at cell centres for block content AND apply optics blur on
top for a "soft-pixel" mode. Considered but scoped out — the "soft"
bleed preset already provides the aesthetic in a cheaper way (widen
smoothstep edges on the dot mask). Revisitable.

### Keep optics running, pixelate post-hoc with a cached processed frame

A second pass where optics+processed-frame feeds a cached buffer
that pixelate then reads at cell centres. Correct but adds an extra
fullscreen pass and a new texture. Rejected as over-engineering for
the same observable result.

## References

- `shaders/layers/pixelate.glsl` — implementation
- `shaders/main.frag` — sample-stage dispatch (`uPixelateStyle != 0`
  path vs `optics_sample` / plain texture fallback)
- [LAYERS.md §Sample stage](../LAYERS.md) — user-facing reference
- `main.cpp::PIXELATE_NAMES` / `PIXELATE_BLEED_NAMES`
