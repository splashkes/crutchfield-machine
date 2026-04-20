// audio.cpp — miniaudio-backed voice pool. See audio.h.

// Some MinGW configurations hide M_PI behind _GNU_SOURCE / _USE_MATH_DEFINES.
// Define it before any math includes to be safe.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "vendor/miniaudio.h"

#include "audio.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
    // ── Sample storage ───────────────────────────────────────────────
    // Each sample is a stereo interleaved float buffer at the device
    // sample rate (mono sources are duplicated). Owned by the audio
    // module; referenced by voices as raw pointers.
    struct Sample {
        std::vector<float> data;     // stereo, L R L R ...
        uint32_t           frames;   // data.size() / 2
        uint32_t           sampleRate;
    };

    std::unordered_map<std::string, Sample> g_samples;
    std::mutex g_samplesMu;          // protects g_samples (writer = main, readers = audio cb)
                                     // audio thread holds a shared-lock path below

    // ── Voice pool ──────────────────────────────────────────────────
    // A single voice can run in one of two modes: playing a preloaded
    // sample, or running a synth oscillator + ADSR. All other state
    // (gain, pan, start frame) is shared so the audio callback can loop
    // over one homogenous array.
    enum VoiceMode { VM_SAMPLE, VM_SYNTH };

    // Biquad (RBJ cookbook forms). Direct Form II Transposed keeps two
    // state variables per channel; cheap, stable enough for our rates.
    struct Biquad {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        float z1 = 0.f, z2 = 0.f;
        bool  active = false;

        void bypass() {
            b0 = 1.f; b1 = 0.f; b2 = 0.f; a1 = 0.f; a2 = 0.f;
            z1 = 0.f; z2 = 0.f; active = false;
        }
        void setLPF(float cutoff, float sr, float Q = 0.707f) {
            if (cutoff <= 20.f || cutoff >= sr * 0.49f) { bypass(); return; }
            float w = (float)(2.0 * M_PI) * cutoff / sr;
            float cw = std::cos(w), sw = std::sin(w);
            float alpha = sw / (2.f * Q);
            float a0 = 1.f + alpha;
            b0 = (1.f - cw) * 0.5f / a0;
            b1 = (1.f - cw)        / a0;
            b2 = b0;
            a1 = -2.f * cw         / a0;
            a2 = (1.f - alpha)     / a0;
            active = true;
        }
        void setHPF(float cutoff, float sr, float Q = 0.707f) {
            if (cutoff <= 20.f || cutoff >= sr * 0.49f) { bypass(); return; }
            float w = (float)(2.0 * M_PI) * cutoff / sr;
            float cw = std::cos(w), sw = std::sin(w);
            float alpha = sw / (2.f * Q);
            float a0 = 1.f + alpha;
            b0 = (1.f + cw) * 0.5f / a0;
            b1 = -(1.f + cw)       / a0;
            b2 = b0;
            a1 = -2.f * cw         / a0;
            a2 = (1.f - alpha)     / a0;
            active = true;
        }
        float process(float x) {
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Voice {
        VoiceMode mode = VM_SAMPLE;

        // Sample mode
        const Sample* sample = nullptr;
        double        pos    = 0.0;
        float         speed  = 1.0f;

        // Synth mode
        float         freq    = 440.f;
        float         phase   = 0.f;
        Audio::Waveform wave  = Audio::WAVE_SAW;
        float         attack  = 0.005f;
        float         decay   = 0.080f;
        float         sustain = 0.70f;
        float         release = 0.150f;
        float         durSec  = 0.25f;
        float         envPos  = 0.f;
        bool          released = false;
        float         envVal   = 0.f;

        // Per-voice filters (stereo pair of biquads each).
        Biquad   lpfL, lpfR;
        Biquad   hpfL, hpfR;

        // Send amounts to global busses (0..1). 0 = dry only.
        float    delaySend = 0.f;
        float    roomSend  = 0.f;

        // Shared
        float    gain        = 1.0f;
        float    panL        = 0.707f;
        float    panR        = 0.707f;
        uint64_t startFrame  = 0;
        bool     active      = false;
    };

    std::vector<Voice> g_voices;

    // Monotonic audio-thread frame counter. Used to schedule voices with
    // sub-frame precision even across callback invocations.
    std::atomic<uint64_t> g_deviceFrame{0};
    uint32_t              g_sampleRate = 48000;

    // ma_device object — stable for the lifetime of the module.
    ma_device g_device;
    bool      g_deviceOk = false;

    // Pending triggers queued by the main thread. Drained at the start
    // of each audio callback into the voice pool.
    struct PendingTrigger {
        VoiceMode     mode;

        // Sample mode
        const Sample* sample = nullptr;
        float         speed  = 1.f;

        // Synth mode
        float         freq = 440.f;
        Audio::Waveform wave = Audio::WAVE_SAW;
        float         attack = 0.005f, decay = 0.080f, sustain = 0.7f, release = 0.15f;
        float         durSec = 0.25f;

        // Effects
        float         lpfCutoff = 0.f;    // 0 = bypass
        float         hpfCutoff = 0.f;
        float         delaySend = 0.f;
        float         roomSend  = 0.f;

        // Shared
        uint64_t      startFrame = 0;
        float         gain = 1.f;
        float         panL = 0.707f, panR = 0.707f;
    };
    std::mutex                  g_pendingMu;
    std::vector<PendingTrigger> g_pending;

    // Equal-power pan law: 0 → (1, 0), 0.5 → (√½, √½), 1 → (0, 1).
    void pan_to_gains(float pan, float& l, float& r) {
        if (pan < 0.f) pan = 0.f;
        if (pan > 1.f) pan = 1.f;
        float theta = pan * (float)(M_PI * 0.5);
        l = std::cos(theta);
        r = std::sin(theta);
    }

    // Sample interpolation (linear). Reads stereo frame at fractional
    // position `pos`, returns (L, R).
    inline void read_stereo(const Sample& s, double pos, float& outL, float& outR) {
        if (pos < 0.0) pos = 0.0;
        uint32_t i0 = (uint32_t)pos;
        if (i0 >= s.frames - 1) {
            outL = s.data[(s.frames - 1) * 2];
            outR = s.data[(s.frames - 1) * 2 + 1];
            return;
        }
        double t  = pos - (double)i0;
        float l0 = s.data[i0 * 2];
        float r0 = s.data[i0 * 2 + 1];
        float l1 = s.data[(i0 + 1) * 2];
        float r1 = s.data[(i0 + 1) * 2 + 1];
        outL = (float)(l0 + (l1 - l0) * t);
        outR = (float)(r0 + (r1 - r0) * t);
    }

    // Assign a pending trigger to the first idle voice, or steal the
    // oldest active one if the pool is full.
    void assign_to_voice(const PendingTrigger& t) {
        int chosen = -1;
        uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < (int)g_voices.size(); i++) {
            if (!g_voices[i].active) { chosen = i; break; }
            if (g_voices[i].startFrame < oldest) {
                oldest = g_voices[i].startFrame;
                chosen = i;
            }
        }
        if (chosen < 0) return;
        Voice& v = g_voices[chosen];
        v.mode       = t.mode;
        v.gain       = t.gain;
        v.panL       = t.panL;
        v.panR       = t.panR;
        v.startFrame = t.startFrame;
        v.active     = true;

        if (t.mode == VM_SAMPLE) {
            v.sample = t.sample;
            v.pos    = 0.0;
            v.speed  = t.speed;
        } else {
            v.freq     = t.freq;
            v.phase    = 0.f;
            v.wave     = t.wave;
            v.attack   = t.attack;
            v.decay    = t.decay;
            v.sustain  = t.sustain;
            v.release  = t.release;
            v.durSec   = t.durSec;
            v.envPos   = 0.f;
            v.released = false;
            v.envVal   = 0.f;
        }

        // Program per-voice filters + send amounts.
        if (t.lpfCutoff > 0.f) {
            v.lpfL.setLPF(t.lpfCutoff, (float)g_sampleRate);
            v.lpfR.setLPF(t.lpfCutoff, (float)g_sampleRate);
        } else {
            v.lpfL.bypass(); v.lpfR.bypass();
        }
        if (t.hpfCutoff > 0.f) {
            v.hpfL.setHPF(t.hpfCutoff, (float)g_sampleRate);
            v.hpfR.setHPF(t.hpfCutoff, (float)g_sampleRate);
        } else {
            v.hpfL.bypass(); v.hpfR.bypass();
        }
        v.delaySend = t.delaySend < 0.f ? 0.f : (t.delaySend > 1.f ? 1.f : t.delaySend);
        v.roomSend  = t.roomSend  < 0.f ? 0.f : (t.roomSend  > 1.f ? 1.f : t.roomSend);
    }

    // ── Global delay bus ────────────────────────────────────────────
    // Fixed-time stereo delay with feedback. Time is set by the music
    // scheduler via Audio::setDelayTime (step 6); default ~0.375s (a
    // dotted eighth at 120 BPM).
    struct DelayBus {
        std::vector<float> bufL, bufR;
        size_t pos = 0;
        float  feedback = 0.45f;
        uint32_t delaySamps = 0;

        void init(float delaySec, uint32_t sr) {
            uint32_t maxSamps = sr * 4;  // up to 4s
            bufL.assign(maxSamps, 0.f);
            bufR.assign(maxSamps, 0.f);
            delaySamps = (uint32_t)(delaySec * sr);
            if (delaySamps < 1) delaySamps = 1;
            if (delaySamps >= maxSamps) delaySamps = maxSamps - 1;
        }
        void process(float inL, float inR, float& outL, float& outR) {
            if (bufL.empty()) { outL = outR = 0.f; return; }
            size_t readPos = (pos + bufL.size() - delaySamps) % bufL.size();
            outL = bufL[readPos];
            outR = bufR[readPos];
            bufL[pos] = inL + outL * feedback;
            bufR[pos] = inR + outR * feedback;
            pos = (pos + 1) % bufL.size();
        }
    };
    DelayBus g_delay;

    // ── Global reverb (Freeverb-style lite) ─────────────────────────
    // 4 comb filters per channel + 3 allpass series. Shorter than the
    // original 8+4 but still makes a decent room sound. Delay lengths
    // rescaled for 48 kHz.
    struct Comb {
        std::vector<float> buf;
        size_t pos = 0;
        float  feedback = 0.84f;
        float  damp = 0.4f;
        float  lpState = 0.f;

        void init(uint32_t samps) { buf.assign(samps, 0.f); pos = 0; }
        float process(float in) {
            if (buf.empty()) return 0.f;
            float out = buf[pos];
            lpState = out * (1.f - damp) + lpState * damp;
            buf[pos] = in + lpState * feedback;
            pos = (pos + 1) % buf.size();
            return out;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        size_t pos = 0;
        float  feedback = 0.5f;
        void init(uint32_t samps) { buf.assign(samps, 0.f); pos = 0; }
        float process(float in) {
            if (buf.empty()) return 0.f;
            float buffered = buf[pos];
            float out = -in + buffered;
            buf[pos] = in + buffered * feedback;
            pos = (pos + 1) % buf.size();
            return out;
        }
    };
    struct ReverbBus {
        Comb    combsL[4], combsR[4];
        Allpass apL[3], apR[3];

        void init(uint32_t sr) {
            // 44.1k Freeverb delays rescaled to current sample rate.
            const uint32_t combL[4] = {1557, 1617, 1491, 1422};
            const uint32_t combR[4] = {1277, 1356, 1188, 1116};
            const uint32_t ap[3]    = {556,  441,  341};
            const float scale = (float)sr / 44100.f;
            for (int i = 0; i < 4; i++) combsL[i].init((uint32_t)(combL[i] * scale));
            for (int i = 0; i < 4; i++) combsR[i].init((uint32_t)(combR[i] * scale));
            for (int i = 0; i < 3; i++) apL[i].init((uint32_t)(ap[i] * scale));
            for (int i = 0; i < 3; i++) apR[i].init((uint32_t)(ap[i] * scale + 23));  // slight stagger
        }
        void process(float inL, float inR, float& outL, float& outR) {
            float sumL = 0.f, sumR = 0.f;
            for (int i = 0; i < 4; i++) { sumL += combsL[i].process(inL); sumR += combsR[i].process(inR); }
            sumL *= 0.25f; sumR *= 0.25f;
            for (int i = 0; i < 3; i++) { sumL = apL[i].process(sumL); sumR = apR[i].process(sumR); }
            outL = sumL; outR = sumR;
        }
    };
    ReverbBus g_reverb;

    // Current envelope value (0..1) for a synth voice, given dt advancement.
    // Steps through A → D → S, then into R once voice.released flips true.
    inline float tick_envelope(Voice& v, float dt) {
        float e = 0.f;
        if (!v.released) {
            float t = v.envPos;
            if (t < v.attack) {
                e = (v.attack > 0) ? (t / v.attack) : 1.f;
            } else if (t < v.attack + v.decay) {
                float dt2 = t - v.attack;
                float f = (v.decay > 0) ? (dt2 / v.decay) : 1.f;
                e = 1.f + (v.sustain - 1.f) * f;
            } else {
                e = v.sustain;
            }
            v.envVal = e;
            v.envPos += dt;
            if (v.envPos >= v.durSec) {
                v.released = true;
                v.envPos   = 0.f;
            }
        } else {
            float t = v.envPos;
            if (t >= v.release) { e = 0.f; v.active = false; }
            else {
                float f = (v.release > 0) ? (t / v.release) : 1.f;
                e = v.envVal * (1.f - f);
            }
            v.envPos += dt;
        }
        return e;
    }

    // Audio callback — miniaudio invokes this on a dedicated high-priority
    // thread. Must be lock-free-ish: we briefly lock the pending queue
    // then drop the lock before the mix loop. No allocation in here.
    void audio_cb(ma_device* /*dev*/, void* outBuf, const void* /*in*/,
                  ma_uint32 frameCount) {
        float* out = (float*)outBuf;
        std::memset(out, 0, frameCount * 2 * sizeof(float));

        // Drain pending triggers.
        {
            std::vector<PendingTrigger> local;
            {
                std::lock_guard<std::mutex> lk(g_pendingMu);
                local.swap(g_pending);
            }
            for (const auto& t : local) assign_to_voice(t);
        }

        const uint64_t blockStart = g_deviceFrame.load(std::memory_order_relaxed);
        const uint64_t blockEnd   = blockStart + frameCount;

        const float dt = 1.f / (float)g_sampleRate;

        // Scratch buffers for the global bus sends. Reset per block.
        // Sized for the worst case (128 or 256 frames typically); a
        // thread-local static avoids allocation.
        static thread_local std::vector<float> sDelayL, sDelayR, sRoomL, sRoomR;
        if (sDelayL.size() < frameCount) {
            sDelayL.assign(frameCount, 0.f); sDelayR.assign(frameCount, 0.f);
            sRoomL .assign(frameCount, 0.f); sRoomR .assign(frameCount, 0.f);
        } else {
            std::fill(sDelayL.begin(), sDelayL.begin() + frameCount, 0.f);
            std::fill(sDelayR.begin(), sDelayR.begin() + frameCount, 0.f);
            std::fill(sRoomL .begin(), sRoomL .begin() + frameCount, 0.f);
            std::fill(sRoomR .begin(), sRoomR .begin() + frameCount, 0.f);
        }

        for (Voice& v : g_voices) {
            if (!v.active) continue;
            if (v.startFrame >= blockEnd) continue;

            uint64_t frameOffset = (v.startFrame > blockStart)
                                 ? (v.startFrame - blockStart) : 0;

            auto emit = [&](ma_uint32 i, float lIn, float rIn) {
                // Filter chain: HPF → LPF per channel.
                float l = v.hpfL.active ? v.hpfL.process(lIn) : lIn;
                float r = v.hpfR.active ? v.hpfR.process(rIn) : rIn;
                l       = v.lpfL.active ? v.lpfL.process(l)   : l;
                r       = v.lpfR.active ? v.lpfR.process(r)   : r;
                float outL = l * v.gain * v.panL;
                float outR = r * v.gain * v.panR;
                out[i * 2]     += outL;
                out[i * 2 + 1] += outR;
                if (v.delaySend > 0.f) { sDelayL[i] += outL * v.delaySend; sDelayR[i] += outR * v.delaySend; }
                if (v.roomSend  > 0.f) { sRoomL [i] += outL * v.roomSend;  sRoomR [i] += outR * v.roomSend;  }
            };

            if (v.mode == VM_SAMPLE) {
                if (!v.sample) { v.active = false; continue; }
                double pos = v.pos;
                const Sample& s = *v.sample;
                for (ma_uint32 i = frameOffset; i < frameCount; i++) {
                    if (pos >= (double)(s.frames - 1)) {
                        v.active = false; v.sample = nullptr; break;
                    }
                    float lIn, rIn;
                    read_stereo(s, pos, lIn, rIn);
                    emit(i, lIn, rIn);
                    pos += v.speed;
                }
                v.pos = pos;
            } else {
                const float phaseInc = v.freq * dt;
                for (ma_uint32 i = frameOffset; i < frameCount; i++) {
                    if (!v.active) break;
                    float sgn = 0.f;
                    switch (v.wave) {
                    case Audio::WAVE_SINE:
                        sgn = std::sin((float)(2.0 * M_PI) * v.phase); break;
                    case Audio::WAVE_SAW:
                        sgn = 2.f * v.phase - 1.f; break;
                    case Audio::WAVE_SQUARE:
                        sgn = (v.phase < 0.5f) ? 1.f : -1.f; break;
                    case Audio::WAVE_TRI:
                        sgn = (v.phase < 0.5f)
                            ? (4.f * v.phase - 1.f)
                            : (3.f - 4.f * v.phase);
                        break;
                    }
                    float env = tick_envelope(v, dt);
                    float s = sgn * env;
                    emit(i, s, s);
                    v.phase += phaseInc;
                    if (v.phase >= 1.f) v.phase -= 1.f;
                }
            }
        }

        // Process the bus sends and mix back onto the master output.
        for (ma_uint32 i = 0; i < frameCount; i++) {
            float dL = 0.f, dR = 0.f, rL = 0.f, rR = 0.f;
            g_delay.process(sDelayL[i], sDelayR[i], dL, dR);
            g_reverb.process(sRoomL[i], sRoomR[i], rL, rR);
            out[i * 2]     += dL + rL * 0.6f;
            out[i * 2 + 1] += dR + rR * 0.6f;
        }

        g_deviceFrame.store(blockEnd, std::memory_order_relaxed);
    }

    // ── WAV loader via miniaudio's decoder ──────────────────────────
    // Decodes the full file into a stereo float buffer at the device rate.
    bool decode_wav(const std::string& path, Sample& out) {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, g_sampleRate);
        ma_decoder dec;
        if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) {
            std::fprintf(stderr, "[audio] decode_wav: can't open %s\n", path.c_str());
            return false;
        }
        ma_uint64 frames = 0;
        ma_decoder_get_length_in_pcm_frames(&dec, &frames);
        if (frames == 0) {
            // Stream-unknown length — decode to an expanding buffer.
            std::vector<float> tmp;
            tmp.reserve(1 << 16);
            const ma_uint64 chunk = 4096;
            std::vector<float> buf(chunk * 2);
            for (;;) {
                ma_uint64 got = 0;
                ma_decoder_read_pcm_frames(&dec, buf.data(), chunk, &got);
                if (got == 0) break;
                tmp.insert(tmp.end(), buf.begin(), buf.begin() + got * 2);
                if (got < chunk) break;
            }
            out.data       = std::move(tmp);
            out.frames     = (uint32_t)(out.data.size() / 2);
            out.sampleRate = g_sampleRate;
        } else {
            out.data.resize(frames * 2);
            ma_uint64 got = 0;
            ma_decoder_read_pcm_frames(&dec, out.data.data(), frames, &got);
            out.frames     = (uint32_t)got;
            out.sampleRate = g_sampleRate;
            out.data.resize(got * 2);
        }
        ma_decoder_uninit(&dec);
        return out.frames > 0;
    }

    // ── Synthetic drum generators ───────────────────────────────────
    // Single-cycle programmatic drums — rough-and-ready but audibly
    // useful until the user drops real WAVs into samples/.
    Sample synth_kick() {
        Sample s; s.sampleRate = g_sampleRate;
        const int len = (int)(0.35 * g_sampleRate);
        s.data.resize(len * 2); s.frames = len;
        for (int i = 0; i < len; i++) {
            double t   = (double)i / g_sampleRate;
            double env = std::exp(-t * 12.0);
            double pitch = 150.0 * std::exp(-t * 40.0) + 50.0;
            double ph  = 2.0 * M_PI * pitch * t;
            float  v   = (float)(std::sin(ph) * env);
            s.data[i*2] = s.data[i*2+1] = v * 0.9f;
        }
        return s;
    }
    Sample synth_snare() {
        Sample s; s.sampleRate = g_sampleRate;
        const int len = (int)(0.22 * g_sampleRate);
        s.data.resize(len * 2); s.frames = len;
        uint32_t seed = 1;
        for (int i = 0; i < len; i++) {
            double t   = (double)i / g_sampleRate;
            double env = std::exp(-t * 24.0);
            // Cheap xorshift noise.
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            float  n   = ((int32_t)seed / (float)INT32_MAX);
            float  tone = (float)(std::sin(2.0 * M_PI * 200.0 * t) * 0.3);
            float  v   = (float)((n * 0.7f + tone) * env);
            s.data[i*2] = s.data[i*2+1] = v * 0.8f;
        }
        return s;
    }
    Sample synth_hat(bool open) {
        Sample s; s.sampleRate = g_sampleRate;
        const double decay = open ? 0.18 : 0.05;
        const int len = (int)(decay * 2 * g_sampleRate);
        s.data.resize(len * 2); s.frames = len;
        uint32_t seed = 42;
        // Highpassed noise via simple one-pole.
        float prev = 0.f;
        for (int i = 0; i < len; i++) {
            double t   = (double)i / g_sampleRate;
            double env = std::exp(-t / decay);
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            float  n   = ((int32_t)seed / (float)INT32_MAX);
            float hp  = n - prev * 0.95f;
            prev = n;
            float v   = (float)(hp * env * 0.4);
            s.data[i*2] = s.data[i*2+1] = v;
        }
        return s;
    }
    Sample synth_clap() {
        Sample s; s.sampleRate = g_sampleRate;
        const int len = (int)(0.18 * g_sampleRate);
        s.data.resize(len * 2); s.frames = len;
        uint32_t seed = 777;
        // Two quick noise bursts separated by a few ms.
        for (int i = 0; i < len; i++) {
            double t = (double)i / g_sampleRate;
            double env = std::exp(-t * 28.0);
            // Add a secondary burst at ~20 ms for the 'clap' doubling.
            if (t > 0.020) env += 0.6 * std::exp(-(t - 0.020) * 28.0);
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            float  n   = ((int32_t)seed / (float)INT32_MAX);
            float v   = (float)(n * env * 0.55);
            s.data[i*2] = s.data[i*2+1] = v;
        }
        return s;
    }
    Sample synth_rim() {
        Sample s; s.sampleRate = g_sampleRate;
        const int len = (int)(0.08 * g_sampleRate);
        s.data.resize(len * 2); s.frames = len;
        for (int i = 0; i < len; i++) {
            double t   = (double)i / g_sampleRate;
            double env = std::exp(-t * 60.0);
            float  v   = (float)(std::sin(2.0 * M_PI * 1700.0 * t) * env * 0.7);
            s.data[i*2] = s.data[i*2+1] = v;
        }
        return s;
    }
    Sample synth_cowbell() {
        Sample s; s.sampleRate = g_sampleRate;
        const int len = (int)(0.25 * g_sampleRate);
        s.data.resize(len * 2); s.frames = len;
        for (int i = 0; i < len; i++) {
            double t   = (double)i / g_sampleRate;
            double env = std::exp(-t * 10.0);
            float v   = (float)((std::sin(2*M_PI*540.0*t) +
                                 std::sin(2*M_PI*800.0*t)) * env * 0.3);
            s.data[i*2] = s.data[i*2+1] = v;
        }
        return s;
    }
}

namespace Audio {

bool init(uint32_t sampleRate, int polyphony) {
    if (g_deviceOk) return true;
    g_sampleRate = sampleRate;
    g_voices.assign(polyphony, Voice{});

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate        = sampleRate;
    cfg.dataCallback      = audio_cb;

    if (ma_device_init(nullptr, &cfg, &g_device) != MA_SUCCESS) {
        std::fprintf(stderr, "[audio] ma_device_init failed\n");
        return false;
    }
    if (ma_device_start(&g_device) != MA_SUCCESS) {
        std::fprintf(stderr, "[audio] ma_device_start failed\n");
        ma_device_uninit(&g_device);
        return false;
    }
    g_deviceOk = true;

    // Effects init — delay defaults to a dotted-eighth at 120 BPM (0.375s),
    // reverb uses Freeverb-style comb/allpass lengths for the current rate.
    g_delay.init(0.375f, sampleRate);
    g_reverb.init(sampleRate);

    std::fprintf(stdout, "[audio] %s @ %u Hz, %d voice pool, fx online\n",
                 ma_get_format_name(ma_format_f32), sampleRate, polyphony);
    return true;
}

void shutdown() {
    if (!g_deviceOk) return;
    ma_device_uninit(&g_device);
    g_deviceOk = false;
    std::lock_guard<std::mutex> lk(g_samplesMu);
    g_samples.clear();
    g_voices.clear();
}

bool running() { return g_deviceOk; }

bool loadSample(const std::string& name, const std::string& path) {
    Sample s;
    if (!decode_wav(path, s)) return false;
    std::lock_guard<std::mutex> lk(g_samplesMu);
    g_samples[name] = std::move(s);
    return true;
}

int loadSamplesFromDir(const std::string& dir) {
    if (!fs::is_directory(dir)) return 0;
    int n = 0;
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (!ent.is_regular_file()) continue;
        fs::path p = ent.path();
        std::string ext = p.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".wav" && ext != ".flac" && ext != ".mp3") continue;
        std::string name = p.stem().string();
        if (loadSample(name, p.string())) n++;
    }
    std::fprintf(stdout, "[audio] loaded %d sample(s) from %s\n", n, dir.c_str());
    return n;
}

int sampleCount() {
    std::lock_guard<std::mutex> lk(g_samplesMu);
    return (int)g_samples.size();
}

bool synthesizeDrum(const std::string& name) {
    Sample s;
    if      (name == "bd"  || name == "kick")    s = synth_kick();
    else if (name == "sn"  || name == "snare")   s = synth_snare();
    else if (name == "hh"  || name == "hat")     s = synth_hat(false);
    else if (name == "oh")                       s = synth_hat(true);
    else if (name == "cp"  || name == "clap")    s = synth_clap();
    else if (name == "rim")                      s = synth_rim();
    else if (name == "cb"  || name == "cowbell") s = synth_cowbell();
    else return false;
    std::lock_guard<std::mutex> lk(g_samplesMu);
    g_samples[name] = std::move(s);
    return true;
}

bool trigger(const std::string& sampleName, const TriggerOpts& opts) {
    if (!g_deviceOk) return false;
    const Sample* sp = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_samplesMu);
        auto it = g_samples.find(sampleName);
        if (it == g_samples.end()) return false;
        sp = &it->second;
    }
    PendingTrigger t;
    t.mode        = VM_SAMPLE;
    t.sample      = sp;
    uint64_t nowF = g_deviceFrame.load(std::memory_order_relaxed);
    double   dly  = opts.delaySec < 0 ? 0 : opts.delaySec;
    t.startFrame  = nowF + (uint64_t)(dly * g_sampleRate);
    t.gain        = opts.gain;
    pan_to_gains(opts.pan, t.panL, t.panR);
    t.speed       = opts.speed <= 0 ? 1.0f : opts.speed;
    t.lpfCutoff   = opts.lpf;
    t.hpfCutoff   = opts.hpf;
    t.delaySend   = opts.delay;
    t.roomSend    = opts.room;
    {
        std::lock_guard<std::mutex> lk(g_pendingMu);
        g_pending.push_back(t);
    }
    return true;
}

bool trigger_note(const NoteOpts& opts) {
    if (!g_deviceOk) return false;
    PendingTrigger t;
    t.mode        = VM_SYNTH;
    uint64_t nowF = g_deviceFrame.load(std::memory_order_relaxed);
    double   dly  = opts.delaySec < 0 ? 0 : opts.delaySec;
    t.startFrame  = nowF + (uint64_t)(dly * g_sampleRate);
    t.gain        = opts.gain;
    pan_to_gains(opts.pan, t.panL, t.panR);
    t.freq        = opts.freqHz > 0 ? opts.freqHz : 440.f;
    t.wave        = opts.wave;
    t.attack      = opts.attack;
    t.decay       = opts.decay;
    t.sustain     = opts.sustain;
    t.release     = opts.release;
    t.durSec      = opts.durationSec > 0 ? opts.durationSec : 0.25f;
    t.lpfCutoff   = opts.lpf;
    t.hpfCutoff   = opts.hpf;
    t.delaySend   = opts.delaySend;
    t.roomSend    = opts.roomSend;
    {
        std::lock_guard<std::mutex> lk(g_pendingMu);
        g_pending.push_back(t);
    }
    return true;
}

// ── Note + waveform helpers ──────────────────────────────────────────
int note_to_midi(const std::string& note) {
    if (note.empty()) return -1;
    // Pure-number form: "60" → MIDI 60.
    bool numeric = true;
    for (char c : note) if (!(c == '-' || c == '.' || (c >= '0' && c <= '9'))) { numeric = false; break; }
    if (numeric) return std::atoi(note.c_str());

    // Letter form: "c4", "c#4", "db4".
    char letter = (char)std::tolower((unsigned char)note[0]);
    int semis;
    switch (letter) {
    case 'c': semis = 0;  break;
    case 'd': semis = 2;  break;
    case 'e': semis = 4;  break;
    case 'f': semis = 5;  break;
    case 'g': semis = 7;  break;
    case 'a': semis = 9;  break;
    case 'b': semis = 11; break;
    default:  return -1;
    }
    size_t idx = 1;
    if (idx < note.size() && note[idx] == '#') { semis++; idx++; }
    else if (idx < note.size() && note[idx] == 'b') { semis--; idx++; }
    int octave = 4;        // default octave = 4 (c4 = middle C = MIDI 60)
    if (idx < note.size()) {
        // parse (possibly negative) octave
        char* endp = nullptr;
        octave = (int)std::strtol(note.c_str() + idx, &endp, 10);
    }
    return 12 * (octave + 1) + semis;
}

float midi_to_freq(int midi) {
    return 440.f * std::pow(2.f, (midi - 69) / 12.f);
}

Waveform waveform_from_name(const std::string& name) {
    std::string n = name;
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    if (n == "sine" || n == "sin")                return WAVE_SINE;
    if (n == "square" || n == "sq")               return WAVE_SQUARE;
    if (n == "tri" || n == "triangle")            return WAVE_TRI;
    return WAVE_SAW;   // default
}

bool is_synth_name(const std::string& name) {
    std::string n = name;
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    return n == "sine" || n == "sin" || n == "saw" || n == "sawtooth"
        || n == "square" || n == "sq" || n == "tri" || n == "triangle";
}

int activeVoices() {
    int n = 0;
    for (const Voice& v : g_voices) if (v.active) n++;
    return n;
}

} // namespace Audio
