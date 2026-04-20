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
    struct Voice {
        const Sample* sample  = nullptr;
        double   pos         = 0.0;   // fractional frame position (for speed != 1)
        float    gain        = 1.0f;
        float    panL        = 0.707f;
        float    panR        = 0.707f;
        float    speed       = 1.0f;
        // Firing time: absolute frame count. When g_deviceFrame reaches it,
        // the voice activates.
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
        const Sample* sample;
        uint64_t      startFrame;
        float         gain;
        float         panL, panR;
        float         speed;
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
        v.sample     = t.sample;
        v.pos        = 0.0;
        v.gain       = t.gain;
        v.panL       = t.panL;
        v.panR       = t.panR;
        v.speed      = t.speed;
        v.startFrame = t.startFrame;
        v.active     = true;
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

        for (Voice& v : g_voices) {
            if (!v.active || !v.sample) continue;

            // If firing time is after this block, skip for now.
            if (v.startFrame >= blockEnd) continue;

            // Compute offset into this block where playback begins.
            uint64_t frameOffset = (v.startFrame > blockStart)
                                 ? (v.startFrame - blockStart) : 0;

            double pos   = v.pos;
            const Sample& s = *v.sample;
            for (ma_uint32 i = frameOffset; i < frameCount; i++) {
                if (pos >= (double)(s.frames - 1)) {
                    v.active = false; v.sample = nullptr;
                    break;
                }
                float lIn, rIn;
                read_stereo(s, pos, lIn, rIn);
                out[i * 2]     += lIn * v.gain * v.panL;
                out[i * 2 + 1] += rIn * v.gain * v.panR;
                pos += v.speed;
            }
            v.pos = pos;
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
    std::fprintf(stdout, "[audio] %s @ %u Hz, %d voice pool\n",
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
    t.sample      = sp;
    uint64_t nowF = g_deviceFrame.load(std::memory_order_relaxed);
    double   dly  = opts.delaySec < 0 ? 0 : opts.delaySec;
    t.startFrame  = nowF + (uint64_t)(dly * g_sampleRate);
    t.gain        = opts.gain;
    pan_to_gains(opts.pan, t.panL, t.panR);
    t.speed       = opts.speed <= 0 ? 1.0f : opts.speed;
    {
        std::lock_guard<std::mutex> lk(g_pendingMu);
        g_pending.push_back(t);
    }
    return true;
}

int activeVoices() {
    int n = 0;
    for (const Voice& v : g_voices) if (v.active) n++;
    return n;
}

} // namespace Audio
