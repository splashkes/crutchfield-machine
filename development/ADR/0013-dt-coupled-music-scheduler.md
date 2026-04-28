# ADR-0013 — Music scheduler advances on frame dt, not wall time

**Status:** Accepted
**Date:** 2026-04-20

## Context

Initial implementation of `Music::update()` advanced the cycle clock
against wall time:

```cpp
g_curCycle = g_cycleAnchor + (now - g_wallOrigin) / cs;
```

`now` came from `glfwGetTime()` which keeps running independent of
render pacing. Each frame, we queried the pattern in
`[g_lastQueryTo, g_curCycle + lookahead]` and fired events with
per-event delaySec clamped to 0.

This produced two user-visible pathologies:

1. **Alt-tab → cascade.** Windows throttles the render thread when the
   window is obscured. Frames drop from 60/s to 2-4/s. Wall time keeps
   advancing, so `g_curCycle` moves a full cycle or more between our
   infrequent `update()` calls. On the next frame, the query spans a
   huge range — events from the past ~second all get scheduled with
   delaySec clamped to 0, producing the "huge slam" burst the user
   described.
2. **Debugger pause / OS stalls** — same shape, different cause. A
   breakpoint pause for a few seconds means a huge query window on
   resume.

Simon also asked for a different but related feature: "if the sim slows
so does the music — kind of cool." He was suggesting dt coupling as a
design choice, but it turned out to also be the fix for the cascade
bug.

## Decision

Advance the cycle clock by per-frame `dt`, clamped:

```cpp
float stepDt = dt;
if (stepDt < 0.f)   stepDt = 0.f;
if (stepDt > 0.1f)  stepDt = 0.1f;   // clamp 100 ms/frame max
g_curCycle += stepDt / cs;
```

The 100 ms clamp is the crucial piece. Any frame whose `dt > 100 ms`
is treated as an execution stall we should *skip*, not replay. Music
pauses for the duration, then resumes where it left off.

Under normal 60 fps operation, `dt ≈ 16.7 ms`, so tempo is
indistinguishable from wall-clock pacing. Under heavy load or alt-tab,
dt is clamped to 100 ms per frame and the scheduler only advances by
that much — no cascade.

## Consequences

**Positive:**
- Alt-tab refocus is silent (no event burst). Simply picks up playing
  from where perceptual time left off.
- "Sim slows → music slows" behavior falls out for free. At 4K60 with
  heavy blur/CA kernels where the sim drops to 30 fps, music tempo
  halves, maintaining perceptual sync with the visuals. Useful for
  the bidirectional-coupling work planned ahead.
- Debugger pauses, long garbage collections in QuickJS, disk stalls
  during preset load — none of them cause playback weirdness.
- Dt-advancement removes the need for the `g_wallOrigin` /
  `g_cycleAnchor` anchoring logic that tried to re-sync cycle time
  on BPM change. BPM change just updates `cs` and the next frame's
  step uses the new cycle length; continuous by construction.

**Negative:**
- If a user caps their framerate very low (e.g. `--fps 15`), music
  tempo won't match BPM exactly — each frame advances 1/15 s, dt is
  well under the clamp but still unusual. At 120 BPM cycle = 2 s,
  the scheduler steps 67 ms at 15 fps = 3.3% of a cycle per frame.
  Music plays at ~120 BPM but with noticeable quantization to the
  frame grid. Acceptable — the bigger win is the stall protection.
- No longer sample-accurate against an absolute clock. If someone
  ever wants to sync to an external hardware timecode that isn't
  coming through MIDI Clock, they'd notice drift. MIDI Clock
  itself arrives on the audio/MIDI thread with real timestamps and
  drives BPM directly, so it's unaffected.

## Invariants locked in

- The music scheduler **must not** read wall-clock time for cycle
  advancement. Only `dt` from the render loop.
- The `dt` clamp applies to every frame. If you find yourself wanting
  to remove it "just for this one case," you're probably re-
  introducing the cascade bug.
- Event firing gates on `hap.whole.begin ∈ [qStart, qEnd)` (see
  ARCHITECTURE invariant #9). Dt-coupling and whole-begin dedupe are
  a pair — either one alone leaves a bug.

## References

- Symptoms reported by Simon: "sometimes a huge slam all togethere,
  other times ticking randomly", "I can trigger a burst of sounds
  easily by alt tabbing in and out".
- Fix commit: `dce81be` ("Music: fix event dedupe + couple tempo to
  frame dt") on the `music` branch.
- Implementation: `music.cpp::update(now, dt, bpm)`.
- Related: ADR-0010 (QuickJS), ADR-0011 (MIDI port), ADR-0012
  (clean-room engine).
