# ADR-0005 — Three-stage recorder: capture → reader pool → encoder pool with shared RAM buffer

**Status:** Accepted
**Date:** 2026-04-19

## Context

Initial recorder design was single-stage: render thread called
`glReadPixels`, immediately wrote EXR to disk. At 4K60 this caused
~200ms stalls per frame — the GPU readback is already slow, and
adding synchronous disk encoding on top meant the render loop
collapsed to ~5 fps during recording.

First refactor (pre-v0.1.0) split capture from write via 8 PBOs + 4
"writer" threads. Each writer thread waited on a fence, mapped the
PBO, did the EXR encode, unmapped. Better, but not enough: EXR ZIP
encoding at 4K is ~300ms/frame on a single thread; 4 writers can do
~13 fps of EXR. Under 60fps capture rate, 77% of frames were dropped.

The root mismatch: **GPU readback is bursty but fast; EXR encoding is
slow but parallelisable.** A ring of PBOs is the wrong back-pressure
reservoir — PBOs are ~66MB each at 4K half-float, and GL doesn't let
you have hundreds of them.

## Decision

Three-stage pipeline with decoupled back-pressure:

1. **Capture** (render thread, fast): `glReadPixels` → PBO + fence →
   push `(pboIdx, fence)` onto `queue_`. Drop the frame if the N=8 PBO
   ring is saturated. Returns in <1ms.
2. **Readers** (N=4 threads with shared GL context): wait fence, map
   PBO, copy (+ float→half convert if precision=32) into a heap
   buffer from the RAM pool, unmap PBO. Push heap buffer onto
   `encodeQueue_`. PBO is immediately reusable.
3. **Encoders** (configurable count, pure CPU, no GL context): pop
   from `encodeQueue_`, call `exr::write_rgba_half` with chosen
   compression, return buffer to pool.

The **RAM pool** (`bufPool_`, `--rec-ram-gb` knob, auto-sized default
`min(freePhys/4, 8GB)`) is the main back-pressure reservoir. 8 GB ÷
66 MB/frame = ~129 frames of buffering on a 4K session — about 2
seconds at 60fps. If encoders can't keep up, the pool drains slowly,
readers block, PBOs back up, captures drop.

Added `--rec-uncompressed` for when users prefer faster writes over
smaller files. ZIP at 4K is the slow path; NONE writes are ~5-10×
faster.

## Consequences

**Positive:**
- Zero-drop 4K60 recording becomes achievable on an RTX 3090 with 8
  encoder threads and uncompressed mode.
- The pipeline is now bandwidth-limited by disk (the thing that
  *should* be the bottleneck) rather than by CPU encoding or GL state.
- The knobs (`--rec-ram-gb`, `--rec-encoders`, `--rec-uncompressed`)
  let users trade memory, CPU, and file size explicitly.

**Negative:**
- The RAM pool at 8GB is a lot of memory. Auto-sizes below on machines
  with less free RAM, but still significant.
- Exit-time drain blocks the main thread: when the user closes the
  app with 100+ frames in `encodeQueue_`, `stop()` synchronously joins
  encoders. With no UI feedback, this looks like a crash. See TODO.md
  for the fix (split stop into begin/finish phases).
- Complexity: three thread pools, two queues, one RAM pool, a shared
  GL context per reader. More to get right. Documented in recorder.cpp
  header comment.

## Alternatives considered

- **Single thread pool that owns everything.** Simpler but doesn't
  address the PBO-vs-encoding mismatch.
- **Disk-backed intermediate ring buffer** (write raw halves to a
  temp file, re-read for EXR encoding). Trades RAM for disk. Rejected
  because disk IO is already the final bottleneck.
- **Skip EXR, write raw dumps, convert post-hoc.** Viable but loses
  the "industry-standard format from the start" advantage of EXR.
- **GPU-side ZIP encoding via compute shader.** Overengineered for
  the win. Rejected.

## References

- `recorder.cpp` header comment — this ADR compressed into the file.
- `recorder.cpp: resolve_config()` — RAM and thread defaults.
- `Recorder::capture`, `readerLoop`, `encoderLoop` — the three stages.
- TODO.md — exit-hang fix is pending work.
