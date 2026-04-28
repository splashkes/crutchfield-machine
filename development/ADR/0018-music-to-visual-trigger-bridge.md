# ADR-0018 — Music → visual bridge: classify at trigger, envelope per bucket

**Status:** Accepted
**Date:** 2026-04-20
**Retroactive:** no

## Context

The existing `fb.*` bridge (ADR-0010 / ADR-0013) publishes **visual
state to the music engine** — zoom, theta, decay, etc. — so patterns
can modulate based on what's on screen. Closing the loop in the
other direction (music influencing visuals) was TODO-P2 ("Audio →
feedback coupling"). Two approaches were considered:

1. **Signal-processing analysis**: continuous RMS + multi-band split
   on the master audio bus, publish as `uAudioLevel`, `uAudioBass`,
   etc. Classical audio-reactivity.
2. **Trigger-level event classification**: tap the discrete voice
   triggers themselves, bucket by sample name / synth frequency,
   publish per-bucket envelopes.

The first option is more general (any audio signal drives visuals,
including live input, external sources, whatever miniaudio is
playing). But it reduces the music to an amplitude envelope — you
lose the *which instrument* information that would let a kick glitch
differently from a snare.

The second option locks the bridge to our internal audio engine —
it won't react to arbitrary waveform characteristics, only to
`Audio::trigger()` / `trigger_note()` calls. But it preserves
musical identity: the shader can know "a kick just fired" vs "a hat
just fired" and respond distinctly.

## Decision

Implement the **trigger-level event classification** bridge. The
signal-processing approach remains open as a future addition; it's
an orthogonal layer (both could coexist).

### Architecture

- **In `audio.cpp`**, each `Audio::trigger(sampleName, opts)` and
  `Audio::trigger_note(opts)` calls a private
  `classify_sample(name, gain)` / frequency check that adds the
  event's gain to a mutex-guarded `TriggerPulses` accumulator with
  five fields: `kick`, `snare`, `hat`, `bass`, `other`.
- **Classification** is prefix match on the Strudel-conventional
  drum-kit short names plus a frequency threshold for synth notes:
  - kick: `"bd"`, `"kick"`
  - snare: `"sn"`, `"sd"`, `"snare"`, `"cp"`, `"rim"`
  - hat: `"hh"`, `"oh"`, `"ch"`, `"hat"`, `"cb"`
  - bass: any synth note with `freqHz < 200`
  - other: everything else (melodic synths above 200 Hz, unrecognised
    sample names)
- **Drain API**: `Audio::consumeTriggerPulses()` atomically returns
  the accumulated pulses and zeros the accumulator. Called once per
  frame from the render loop.
- **Host-side envelope**: main loop folds pulses into five `State`
  fields (`musKick`, `musSnare`, `musHat`, `musBass`, `musOther`)
  with a rise-on-trigger, decay-0.88-per-frame envelope (~150 ms
  half-life at 60 fps).
- **Uniforms**: `uMusKick/Snare/Hat/Bass/Other` are pushed to the
  feedback shader every frame.
- **Consumers**: currently the noise layer's `dropout` archetype
  (mode 4) reads the envelopes and produces per-bucket glitch
  flavours — wide black blocks for kick, white flashes for snare,
  green speckle for hat, hue-rotating blocks for bass, rainbow
  glitches for other.

## Consequences

### Wins

- Musical identity survives the bridge. Visuals can respond to a
  hat differently than a kick, which an RMS+bands pipeline can't
  easily do.
- Zero-allocation, zero-FFT — classification is a string compare and
  a float compare. Imperceptible overhead.
- Extends naturally: adding a new bucket means adding a field to
  `TriggerPulses` + a new `uMus*` uniform + a consumer branch.
- Clean separation: audio thread writes to the accumulator under a
  mutex, render thread drains once per frame. No locking on the
  hot render path.

### Costs

- **Bridge is internal-only.** Live line-in audio, DAW output,
  ambient sound — none of it fires `Audio::trigger`, so none of it
  reaches the shader. If audio-reactivity to arbitrary waveforms is
  ever required, that's a second (complementary) bridge based on
  the RMS/bands approach.
- Classification rules are conservative and prefix-based. If
  someone names a sample "bd1" it matches the prefix "bd" (OK). If
  they name it "kick_main" it doesn't match "kick" exactly (it's
  caught in the `name == "kick"` check, not a prefix check —
  intentional to avoid false matches). Authors of custom sample
  packs may need to rename for classification to work.
- Five fixed buckets. If someone wants "clap" separate from "snare"
  they have to add a bucket.

### Non-consequences

- Does not change how music plays. Classification is a side-effect
  of the existing trigger; the audio pipeline is untouched.
- Does not add a new thread. Runs on the audio thread when triggers
  fire; render thread drains via the existing per-frame tick.
- Does not commit us to keeping these exact bucket names. Bucket
  set and envelope decay rate are internal; only the uniform names
  leak into shader code, and those are straightforward to rename.

## Alternatives considered

### RMS + 3-band split (not mutually exclusive; deferred)

Standard audio-reactivity: short-window RMS on the master bus, with
an FFT-free 3-band split (low/mid/high via cascaded biquads or a
single pass with cheap band-limit approximations). Good general
coupling, loses musical identity. Deferred — compatible with the
trigger bridge if both are wanted.

### Onset detection

Spectral-flux onset detector firing a single "onset pulse" uniform.
Loses musical identity. Most of what we'd want is captured by the
kick bucket anyway.

### Expose the voice-pool state directly

Shader reads e.g. `uActiveVoiceCount`, `uLastTriggerSec`, etc. Less
structured; consumers have to write classification themselves.
Inside-out of the current design.

### Publish pulses as JS-side `fb.*` first, then through the audio
### → JS → shader path

Would go through the existing JS bridge. Overkill (and introduces a
one-frame lag through JS). Native C++ is one hop shorter.

## References

- `audio.h` — `TriggerPulses`, `consumeTriggerPulses()`.
- `audio.cpp` — `classify_sample`, `g_pulses` accumulator, pulse
  updates in `trigger()` / `trigger_note()`.
- `main.cpp` — envelope decay + uniform push (after the `Music::
  setScalar` block).
- `shaders/layers/noise.glsl` — dropout-mode consumer.
- [LAYERS.md §Music → visual bridge](../LAYERS.md#music--visual-bridge-dropout-noise)
