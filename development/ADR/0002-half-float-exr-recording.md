# ADR-0002 — Half-float RGBA EXR as the archival recording format

**Status:** Accepted
**Date:** 2026-04-19 (retroactive)
**Retroactive:** yes

## Context

"Recording" in video tooling usually means H.264/HEVC MP4. For a
dynamical-systems project where the *simulation state* is the thing
worth preserving, that pipeline is lossy at every step:

1. Read display framebuffer (already 8-bit, post-tonemapping).
2. Convert to YUV 4:2:0 (chroma-subsample loss).
3. H.264-encode (quantization loss + motion-vector artifacts).

By the time the MP4 lands on disk, the archived content is several
steps removed from what the simulation produced. For a research tool
where users may want to analyse, re-process, or re-render the captured
state, this is disqualifying.

Available lossless options:

- **PNG sequence (RGB 8-bit).** Lossless in the codec sense, but still
  quantised to 8-bit at capture. Same fundamental issue as MP4.
- **TIFF 16-bit integer.** Better range, but integer-domain can't
  represent values > 1.0 that the HDR-ish sim produces.
- **EXR half-float** (OpenEXR-compatible). 16-bit float per channel,
  unbounded range (encodes ±65504 with gradual underflow). Originally
  developed by ILM for film compositing.
- **Raw float dumps.** Bit-exact, but no format spec — nobody can open
  them without custom code.

EXR with ZIP compression yields ~2-3× size reduction vs. uncompressed;
uncompressed writes ~5-10× faster (critical at 4K60).

## Decision

Record lossless half-float RGBA EXR sequences, captured *directly from
the simulation FBO* (not the display framebuffer). Support both ZIP
(default, smaller) and uncompressed (faster) via `--rec-uncompressed`.

## Consequences

**Positive:**
- The archived data matches what the sim produced, bit-exact within
  half-float precision.
- Industry-standard format: any VFX/DCC tool (Nuke, Fusion, Blender,
  DaVinci Resolve, ffmpeg via libOpenEXR) can read the output.
- No external dependency — we ship a 300-line self-contained EXR writer
  in `exr_write.h` (RGBA half-float, ZIP or none, no other features).
- Users can re-render to any modern codec after the fact via ffmpeg.

**Negative:**
- File sizes are an order of magnitude larger than equivalent H.264.
  A 4K60 minute ≈ 3-4 GB ZIP, ~15 GB uncompressed.
- Encoder CPU cost at 4K60 is real; required the 3-stage pipeline
  (see ADR-0005) to not drop frames.
- Users expect MP4s for sharing; they must run ffmpeg themselves.
  Mitigated by the on-exit encode prompt in main.cpp.

## Alternatives considered

- **Ship an optional MP4 encoder path alongside EXR.** Would save users
  the ffmpeg step but would double the binary size (libav), complicate
  the build, and contradict the "no codec lock-in" goal.
- **DPX / Cineon.** 10-bit log, used in film pipelines. Narrower tool
  support than EXR and no float range.
- **Record the display framebuffer instead of the sim FBO.** Simpler
  but loses precision at the moment of capture. The whole point of the
  recorder is to preserve sim state; recording the display defeats it.

## References

- `exr_write.h` — the self-contained writer.
- `recorder.cpp` — capture pipeline and compression selection.
- DESIGN.md — "Lossless when archival matters" principle.
- [OpenEXR spec](https://openexr.com/en/latest/) — we implement a minimal subset.
