# ADR-0004 — One `.glsl` file per layer, dispatched via bitfield from `main.frag`

**Status:** Accepted
**Date:** 2026-04-19 (retroactive — this was the original project structure)
**Retroactive:** yes

## Context

A video feedback system accumulates many independent processing stages
over time: warp, blur, noise, decay, color shifts, chromatic aberration,
camera mixing, etc. How to structure the shader code to make adding,
removing, and toggling these stages cheap?

Options:

1. **One monolithic `main.frag`** that dispatches on a string or int
   parameter. Quick to start, terrible to maintain — every change
   touches one file and one function.
2. **Multiple programs, swap between them per frame.** Each layer is
   its own full shader program, selected at dispatch. Lots of state
   change overhead; layers can't naturally compose.
3. **Runtime-compiled program**, generating a `main.frag` by
   concatenating active layers. Most flexible, most complex — requires
   a preprocessor pass on every parameter change.
4. **One `.glsl` file per layer, `#include`-resolved into a single
   `main.frag` that dispatches by bitfield.** Compiled once; per-frame
   cost is just the dynamic branch on the enable bits.

Layers naturally have a shared shape: `layer_apply(color, ...) → color`.
They read a small set of uniforms named by convention. Option 4 makes
this shape explicit.

## Decision

Each layer lives in `shaders/layers/<name>.glsl` as a single function
(`<name>_apply`). `main.frag` manually `#include`s every layer and
dispatches each via a bit in the `uEnable` uniform. The host resolves
`#include` directives before sending to the GLSL compiler.

Host mirror: `main.cpp` has an `L_*` enum matching the GLSL `const int
L_*` bits. Keyboard F-keys toggle these bits.

## Consequences

**Positive:**
- Adding a layer is ~5 lines of GLSL + 3 lines of host wiring. See
  CONTRIBUTING.md for the walkthrough.
- Each layer is legible in isolation — no mental tracking of what
  "stage 7" is in a monolithic pipeline.
- Hot-reload (`\` key) rebuilds the whole `main.frag` + layers in a
  few ms. No per-layer compile state to juggle.
- Presets can save/restore which layers are on — just the bitfield.

**Negative:**
- Layer dispatch order is fixed at source-edit time; runtime can only
  toggle on/off, not reorder. For this project's needs that's fine;
  some feedback configurations may want arbitrary order in the future.
- Host-side bit enum and GLSL-side bit constants are hand-mirrored.
  Getting them out of sync silently breaks a toggle. Caught by testing,
  not by the compiler.
- Every uniform a layer reads is declared in `main.frag`, not in the
  layer's own file. Adding a layer means touching two files minimum.

## Alternatives considered

- **Dynamic program composition** (option 3 above). Too much
  infrastructure for a project this size. Reconsider if we ever want
  user-defined dispatch orders.
- **GL 4.3 subroutines.** Would give per-layer granularity in a single
  program, but subroutine support on consumer Windows drivers has
  historically been flaky, and they don't hot-reload any better than
  our `#include` approach.

## References

- `shaders/main.frag` — orchestrator, `#include` list around line 57,
  dispatch list around line 90.
- `shaders/layers/*.glsl` — the layer files themselves.
- `main.cpp` — `L_*` enum at ~line 124; `LAYERS[]` table for F-key
  bindings; `reload_shaders()` for the `#include` resolver.
- CONTRIBUTING.md — end-to-end "add a sharpen layer" walkthrough.
