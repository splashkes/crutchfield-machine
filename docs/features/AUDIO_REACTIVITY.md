# Built-in audio reactivity

Crutchfield's music-engine output (the audio Crutchfield is playing through its built-in player) is analyzed in real time and exposed as 5 envelope values that any binding can subscribe to. No TouchDesigner or external audio analysis required.

## Channels

| Channel | What it measures |
| --- | --- |
| `rms` | overall loudness, smoothed over each audio block |
| `peak` | sample-peak amplitude, fast attack + slow release |
| `low` | energy below ~200 Hz (kick, sub) |
| `mid` | energy 200 Hz – 2000 Hz (snare body, vocal mids) |
| `high` | energy above ~2000 Hz (hi-hat, air, transients) |

All five are envelope-followed (5 ms attack, 80 ms release), so the values move smoothly and don't flicker on every sample.

Output range: 0..~1 typical; can spike slightly above 1 on strong transients. Most consumers want to clamp or scale.

## Bindings

`audio:CHANNEL` is valid in any binding section. The keyPart prefix decides the source type, not the section header.

```ini
[osc]              ; section irrelevant — any header works
dyn.decay.axis           = audio:rms scale=2.0
color.sat.setAxis        = audio:mid bipolar
warp.zoom.axis           = audio:low scale=0.5
optics.chroma+           = audio:high scale=0.1 delta
layer.noise              = audio:peak           ; discrete: fires when peak > 0.5
pattern.spiral           = audio:high           ; discrete: fires when high band > 0.5
```

The same flags that apply to OSC bindings work here:

| Flag | Effect |
| --- | --- |
| `scale=X` | multiply envelope value before dispatch |
| `invert` | negate after scale (turn high audio = lower parameter) |
| `bipolar` | remap 0..1 → −1..+1 (right for centered controls) |

## Dispatch shape

Audio bindings dispatch every frame at render rate (~60 Hz typical) with the current envelope value. The action's `kind` decides the shape:

| Action kind | Dispatch |
| --- | --- |
| `AK_STEP` / `AK_RATE` | continuous value through `handler_(action, val)` |
| `AK_DISCRETE` | fires once on the frame value crosses > 0.5 |
| `AK_TRIGGER` | fires both edges (value > 0.5 = press, ≤ 0.5 = release) |

For continuous parameters like `dyn.decay.axis` this gives smooth modulation. For layer toggles you probably want OSC triggers or keyboard, not audio.

## How it works

The analyzer lives at the end of `audio_cb()` in `audio.cpp`. Every audio block (typical ~128 frames at 48 kHz = ~2.7 ms) runs through:

1. Mono mix (L+R / 2)
2. Two cascaded 1-pole lowpass filters at 200 Hz → `low`
3. Two cascaded 1-pole lowpass filters at 2000 Hz, minus `low` → `mid`
4. Signal minus the 2 kHz lowpass → `high`
5. Envelope followers on each band (rectify + asymmetric attack/release smoothing)
6. RMS via sum-of-squares over the block
7. Sample-peak from per-sample abs max
8. Five atomic writes to `g_analyzer_rms` / `peak` / `low` / `mid` / `high`

The main thread reads the atomics once per frame in `Input::pollAudio(dt)` and dispatches matching bindings. Lock-free across the thread boundary.

## What gets analyzed

Currently: Crutchfield's own audio engine output. This is what the music-preset system plays (metronome, samples, synthesized voices). The audio device's playback callback fills `outBuf` and the analyzer reads from `outBuf` AFTER the synthesis pass.

What's NOT analyzed:
- System input audio (microphone, line in) — requires a capture device, future work
- Audio from other apps (Ableton, browser, etc.) — same; requires audio routing
- Pre-rendered music files — only what Crutchfield's player actually plays

If you want external audio reactivity now:
- Route external audio through Crutchfield's player (load as a sample preset, see `music/`)
- OR send analyzed values from TouchDesigner over OSC (see [COOKBOOK.md recipe 1](../osc/COOKBOOK.md#1-audio-reactive-decay-from-touchdesigner))

## Performance

Per-sample cost: ~12 muls / 6 adds across all bands and envelopes. At 48 kHz with ~128-sample blocks that's ~0.07% of CPU. Atomic writes are uncontested (5 writers, 1 reader, no synchronization needed beyond `memory_order_relaxed`).

## Tuning

The defaults (200 Hz / 2000 Hz crossovers, 5 ms / 80 ms envelope) are tuned for typical electronic / pop music. To customize:

- Edit the alpha values in `analyzer_process_block()` in `audio.cpp` and rebuild
- Or apply `scale` flags in bindings to compensate (`audio:low scale=0.3` if low band is too dominant)

A future enhancement might expose the crossovers + envelope times as `[audio.config]` keys. Not yet.

## Cookbook

### Decay follows loudness

```ini
dyn.decay.axis = audio:rms scale=2.0 invert
```

Loud sections → low decay (jittery, alive). Quiet sections → high decay (lingering, dreamy).

### Hue rotation chases the kick

```ini
color.hue.setAxis = audio:low scale=4.0
```

Big bass = hue shifts quickly. Useful for techno.

### Saturation follows mid-range

```ini
color.sat.setAxis = audio:mid scale=1.5
```

Vocals and snares bump saturation. Tracks energy without being kick-driven.

### Pattern strobe on hi-hat

```ini
pattern.starburst = audio:high
```

`AK_DISCRETE` semantics: fires once when the high band crosses 0.5. Good for hi-hat-driven strobe effects.

## Implementation pointers

- `audio.cpp`: `g_analyzer_*` atomics, `analyzer_process_block()`, `extern "C"` getters at the bottom
- `input.h/cpp`: `SRC_AUDIO` binding source, `audio:` keyPart parser, `Input::pollAudio()`
- `main.cpp`: `g_input.pollAudio(dt)` in the per-frame poll block
