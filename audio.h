// audio.h — realtime audio output + voice pool + sampler.
//
// Wraps miniaudio as the device backend. Owns a fixed-size polyphonic
// voice pool; callers schedule sample triggers with a delay (seconds)
// and the audio thread fires them at the right time.
//
// Step 3 scope: WAV sample playback with gain/pan/speed. Synthesized
// fallback drums (kick/snare/hat/clap) so the app makes noise even
// when samples/ is empty. Later steps add synth voices + effects.

#pragma once

#include <string>
#include <cstdint>

namespace Audio {

// Initialize the audio device + voice pool. Idempotent. Returns false
// (and prints to stderr) on failure; app keeps running silently.
bool init(uint32_t sampleRate = 48000, int polyphony = 24);
void shutdown();

// Returns true if init() succeeded and the device is streaming.
bool running();

// Sample loading. Samples are addressed by name in trigger(). Loading
// the same name twice replaces the buffer. Use the `loadDir` helper to
// pick up everything under samples/ in one call.
bool loadSample(const std::string& name, const std::string& path);
int  loadSamplesFromDir(const std::string& dir);
int  sampleCount();

// Synthesize a placeholder drum named `name` and register it under that
// name. Supports "bd", "sn", "hh", "cp", "oh", "cb", "rim". Useful for
// the empty-samples-folder bootstrap.
bool synthesizeDrum(const std::string& name);

struct TriggerOpts {
    double delaySec = 0.0;   // seconds from now to fire
    float  gain     = 1.0f;
    float  pan      = 0.5f;  // 0=left, 0.5=center, 1=right
    float  speed    = 1.0f;  // playback rate (pitch-shifts)
    // Reserved for step 5 effects; audio stage uses them as-is today.
    float  lpf      = 0.0f;
    float  hpf      = 0.0f;
    float  room     = 0.0f;
    float  delay    = 0.0f;
};

// Queue a sample voice. Returns true on success; drops silently if
// the sample name is unknown or the pool is saturated.
bool trigger(const std::string& sampleName, const TriggerOpts& opts);

// Synth waveforms for trigger_note().
enum Waveform {
    WAVE_SINE = 0,
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRI,
};

struct NoteOpts {
    double delaySec    = 0.0;
    float  durationSec = 0.25f;   // sustain window before release
    float  freqHz      = 440.0f;
    Waveform wave      = WAVE_SAW;
    float  gain        = 0.35f;   // synths are loud; conservative default
    float  pan         = 0.5f;
    // ADSR in seconds / sustain level.
    float  attack      = 0.005f;
    float  decay       = 0.080f;
    float  sustain     = 0.70f;
    float  release     = 0.150f;
};

// Queue a pitched synth voice.
bool trigger_note(const NoteOpts& opts);

// "c4" → 60, "c#4" / "db4" → 61, "60" → 60. Returns -1 on parse error.
int note_to_midi(const std::string& note);

// MIDI note number → frequency in Hz. 69 = A4 = 440.
float midi_to_freq(int midi);

// "saw" / "square" / "sine" / "tri" (or Strudel's "triangle"). Default SAW.
Waveform waveform_from_name(const std::string& name);

// True if `name` is a synth waveform rather than a sample slot.
bool is_synth_name(const std::string& name);

// Runtime stats (for HUD).
int  activeVoices();

} // namespace Audio
