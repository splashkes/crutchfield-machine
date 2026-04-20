// music.cpp — QuickJS embedding entry point. See music.h.
//
// Step 1 scope: init runtime, expose print() to JS, run a smoke eval.
// Everything else (pattern engine, audio, scheduler) gets stacked on
// top of this module in subsequent steps.

#include "music.h"
#include "audio.h"

extern "C" {
#include "quickjs.h"
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
    JSRuntime* g_rt  = nullptr;
    JSContext* g_ctx = nullptr;
    bool       g_up  = false;

    // Native implementation of `print(...)` — JS side calls print("hi").
    // Prefixed with [js] so console output is easy to filter.
    JSValue js_print(JSContext* ctx, JSValueConst /*this_val*/,
                     int argc, JSValueConst* argv) {
        std::fputs("[js] ", stdout);
        for (int i = 0; i < argc; i++) {
            if (i) std::fputc(' ', stdout);
            const char* s = JS_ToCString(ctx, argv[i]);
            if (!s) return JS_EXCEPTION;
            std::fputs(s, stdout);
            JS_FreeCString(ctx, s);
        }
        std::fputc('\n', stdout);
        return JS_UNDEFINED;
    }

    // Pretty-print a JS exception — name, message, stack — to stderr.
    void dump_exception(JSContext* ctx) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        std::fprintf(stderr, "[js] exception: %s\n", msg ? msg : "<unprintable>");
        if (msg) JS_FreeCString(ctx, msg);

        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* st = JS_ToCString(ctx, stack);
            if (st) {
                std::fprintf(stderr, "[js] stack:\n%s\n", st);
                JS_FreeCString(ctx, st);
            }
        }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, exc);
    }
}

namespace Music {

bool init() {
    if (g_up) return true;

    g_rt = JS_NewRuntime();
    if (!g_rt) {
        std::fprintf(stderr, "[music] JS_NewRuntime failed\n");
        return false;
    }
    // 32 MB memory ceiling — plenty for pattern code; guards runaway
    // evaluations from trashing the host process.
    JS_SetMemoryLimit(g_rt, 32 * 1024 * 1024);
    // 1 MB stack ceiling — Strudel patterns recurse fairly deeply.
    JS_SetMaxStackSize(g_rt, 1 * 1024 * 1024);

    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) {
        std::fprintf(stderr, "[music] JS_NewContext failed\n");
        JS_FreeRuntime(g_rt); g_rt = nullptr;
        return false;
    }

    // Expose our print() helper as a global so smoke tests and Strudel's
    // console.log-style calls can land somewhere useful.
    JSValue global = JS_GetGlobalObject(g_ctx);
    JS_SetPropertyStr(g_ctx, global, "print",
                      JS_NewCFunction(g_ctx, js_print, "print", 1));
    // Minimal console.log shim — points at the same print function.
    JSValue console = JS_NewObject(g_ctx);
    JS_SetPropertyStr(g_ctx, console, "log",
                      JS_NewCFunction(g_ctx, js_print, "log", 1));
    JS_SetPropertyStr(g_ctx, global, "console", console);
    JS_FreeValue(g_ctx, global);

    g_up = true;
    std::fprintf(stdout, "[music] QuickJS %s runtime up\n", JS_GetVersion());

    // Load the pattern engine. Path resolves relative to the executable
    // so it works when launched from Explorer or from any shell.
    static const char* search[] = {
        "js/engine.js",
        "./js/engine.js",
    };
    std::string code;
    for (const char* p : search) {
        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::stringstream ss; ss << f.rdbuf();
        code = ss.str();
        if (!code.empty()) { break; }
    }
    if (code.empty()) {
        std::fprintf(stderr, "[music] js/engine.js not found — pattern engine unavailable\n");
        return true;  // runtime still usable for smoketests
    }
    if (!eval(code, "js/engine.js")) {
        std::fprintf(stderr, "[music] engine.js failed to load cleanly\n");
    }
    return true;
}

void shutdown() {
    if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = nullptr; }
    if (g_rt)  { JS_FreeRuntime(g_rt);  g_rt  = nullptr; }
    g_up = false;
}

bool eval(const std::string& code, const std::string& tag) {
    if (!g_up) return false;
    JSValue r = JS_Eval(g_ctx, code.c_str(), code.size(),
                        tag.c_str(), JS_EVAL_TYPE_GLOBAL);
    bool ok = !JS_IsException(r);
    if (!ok) dump_exception(g_ctx);
    JS_FreeValue(g_ctx, r);
    return ok;
}

namespace {
    // Pull a named numeric prop off a JS object, or fallback if missing.
    double get_num(JSContext* ctx, JSValueConst obj, const char* name, double fallback) {
        JSValue v = JS_GetPropertyStr(ctx, obj, name);
        if (JS_IsUndefined(v) || JS_IsNull(v)) { JS_FreeValue(ctx, v); return fallback; }
        double d = fallback;
        if (JS_ToFloat64(ctx, &d, v) < 0) d = fallback;
        JS_FreeValue(ctx, v);
        return d;
    }
    std::string get_str(JSContext* ctx, JSValueConst obj, const char* name) {
        JSValue v = JS_GetPropertyStr(ctx, obj, name);
        if (JS_IsUndefined(v) || JS_IsNull(v)) { JS_FreeValue(ctx, v); return {}; }
        std::string out;
        // If it's a number, format it. If it's a string, use it. Anything
        // else: skip.
        if (JS_IsString(v)) {
            const char* s = JS_ToCString(ctx, v);
            if (s) { out = s; JS_FreeCString(ctx, s); }
        } else if (JS_IsNumber(v)) {
            double d = 0; JS_ToFloat64(ctx, &d, v);
            char buf[64]; std::snprintf(buf, sizeof buf, "%g", d);
            out = buf;
        }
        JS_FreeValue(ctx, v);
        return out;
    }
}

std::vector<Event> query(const std::string& code, double begin, double end) {
    std::vector<Event> out;
    if (!g_up) return out;

    // Build: (function(){ return (CODE).queryArc(BEGIN, END); })()
    // Wrap user code parenthesized in case it contains semicolons.
    char header[128];
    std::snprintf(header, sizeof header,
                  "(function(){return (");
    char tail[128];
    std::snprintf(tail, sizeof tail, ").queryArc(%.9f,%.9f);})()", begin, end);
    std::string full = std::string(header) + code + tail;

    JSValue arr = JS_Eval(g_ctx, full.c_str(), full.size(),
                          "<query>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(arr)) {
        dump_exception(g_ctx);
        JS_FreeValue(g_ctx, arr);
        return out;
    }
    if (!JS_IsArray(arr)) {
        std::fprintf(stderr, "[music] query: expected array, got non-array\n");
        JS_FreeValue(g_ctx, arr);
        return out;
    }
    JSValue lenv = JS_GetPropertyStr(g_ctx, arr, "length");
    uint32_t len = 0; JS_ToUint32(g_ctx, &len, lenv); JS_FreeValue(g_ctx, lenv);

    for (uint32_t i = 0; i < len; i++) {
        JSValue h = JS_GetPropertyUint32(g_ctx, arr, i);
        if (!JS_IsObject(h)) { JS_FreeValue(g_ctx, h); continue; }

        JSValue part = JS_GetPropertyStr(g_ctx, h, "part");
        JSValue val  = JS_GetPropertyStr(g_ctx, h, "value");

        Event e;
        e.begin   = get_num(g_ctx, part, "begin", 0.0);
        e.end     = get_num(g_ctx, part, "end",   0.0);
        e.sample  = get_str(g_ctx, val,  "s");
        e.note    = get_str(g_ctx, val,  "note");
        e.gain    = get_num(g_ctx, val,  "gain",   1.0);
        e.pan     = get_num(g_ctx, val,  "pan",    0.5);
        e.speed   = get_num(g_ctx, val,  "speed",  1.0);
        e.lpf     = get_num(g_ctx, val,  "lpf",    0.0);
        e.hpf     = get_num(g_ctx, val,  "hpf",    0.0);
        e.room    = get_num(g_ctx, val,  "room",   0.0);
        e.delay   = get_num(g_ctx, val,  "delay",  0.0);
        e.channel = (int)get_num(g_ctx, val, "channel", 0);
        out.push_back(std::move(e));

        JS_FreeValue(g_ctx, part);
        JS_FreeValue(g_ctx, val);
        JS_FreeValue(g_ctx, h);
    }
    JS_FreeValue(g_ctx, arr);
    return out;
}

// ── Scheduler ────────────────────────────────────────────────────────
// One cycle = 4 beats at the current BPM, matching Strudel's default
// cps of 0.5 at 120 BPM. update() runs on the main thread; it queries
// the active pattern over a small look-ahead window and pushes any
// events that haven't been triggered yet into the audio module.

namespace {
    std::string g_pattern;        // active pattern expression ("" = stopped)
    bool        g_playing    = false;
    // Wall time when cycle 0 was "anchored". When BPM changes we
    // re-anchor so the cycle rate is continuous.
    double      g_wallOrigin  = 0.0;
    double      g_cycleAnchor = 0.0;
    double      g_lastQueryTo = 0.0;   // cycle-time up to which we've fired
    double      g_lastBpm     = 120.0;
    double      g_curCycle    = 0.0;   // updated by update() each frame
    const double kLookahead   = 0.125; // cycles — ~250ms at 120 BPM

    double cycle_seconds(float bpm) {
        if (bpm < 1.0f) bpm = 1.0f;
        return 240.0 / (double)bpm;    // 4 beats per cycle
    }
}

void setPattern(const std::string& code) {
    g_pattern = code;
    if (!code.empty()) {
        std::fprintf(stdout, "[music] pattern set: %s\n", code.c_str());
        g_lastQueryTo = g_cycleAnchor;   // re-fire from the anchor
    } else {
        std::fprintf(stdout, "[music] pattern cleared\n");
    }
}

const std::string& pattern() { return g_pattern; }

void setPlaying(bool on) {
    if (on == g_playing) return;
    g_playing = on;
    std::fprintf(stdout, "[music] %s\n", on ? "playing" : "paused");
}

bool playing() { return g_playing; }

void update(double now, float bpm) {
    if (!g_up) return;
    if (g_wallOrigin == 0.0) { g_wallOrigin = now; g_lastBpm = bpm; }

    const double cs = cycle_seconds(bpm);
    // Advance the cycle clock from the current anchor.
    g_curCycle = g_cycleAnchor + (now - g_wallOrigin) / cs;

    // BPM change: re-anchor at the current cycle so the cycle rate is
    // continuous across tempo edits.
    if (std::fabs((double)bpm - g_lastBpm) > 0.01) {
        g_cycleAnchor = g_curCycle;
        g_wallOrigin  = now;
        g_lastBpm     = bpm;
        g_lastQueryTo = g_curCycle;
    }

    if (!g_playing || g_pattern.empty()) return;

    // Query window: from last-fired up to now + lookahead.
    double qStart = g_lastQueryTo;
    double qEnd   = g_curCycle + kLookahead;
    if (qEnd <= qStart) return;

    auto evs = query(g_pattern, qStart, qEnd);
    for (const auto& e : evs) {
        double delaySec = (e.begin - g_curCycle) * cs;
        if (delaySec < 0.0) delaySec = 0.0;

        // Branch on what's attached to the hap:
        //   • note set (with or without s=saw/square/…) → synth voice
        //   • s set to a sample name → sample trigger
        //   • nothing → ignore
        if (!e.note.empty()) {
            int midi = Audio::note_to_midi(e.note);
            if (midi < 0) continue;
            Audio::NoteOpts n;
            n.delaySec    = delaySec;
            n.freqHz      = Audio::midi_to_freq(midi);
            n.wave        = Audio::is_synth_name(e.sample)
                          ? Audio::waveform_from_name(e.sample)
                          : Audio::WAVE_SAW;
            n.durationSec = (float)((e.end - e.begin) * cs);
            n.gain        = (float)e.gain * 0.35f;
            n.pan         = (float)e.pan;
            n.lpf         = (float)e.lpf;
            n.hpf         = (float)e.hpf;
            n.delaySend   = (float)e.delay;
            n.roomSend    = (float)e.room;
            Audio::trigger_note(n);
            continue;
        }

        if (e.sample.empty()) continue;

        if (Audio::is_synth_name(e.sample)) {
            Audio::NoteOpts n;
            n.delaySec    = delaySec;
            n.wave        = Audio::waveform_from_name(e.sample);
            n.durationSec = (float)((e.end - e.begin) * cs);
            n.gain        = (float)e.gain * 0.35f;
            n.pan         = (float)e.pan;
            n.lpf         = (float)e.lpf;
            n.hpf         = (float)e.hpf;
            n.delaySend   = (float)e.delay;
            n.roomSend    = (float)e.room;
            Audio::trigger_note(n);
            continue;
        }

        // Drum-style sample path.
        Audio::synthesizeDrum(e.sample);           // no-op if already loaded
        Audio::TriggerOpts t;
        t.delaySec = delaySec;
        t.gain     = (float)e.gain;
        t.pan      = (float)e.pan;
        t.speed    = (float)e.speed;
        t.lpf      = (float)e.lpf;
        t.hpf      = (float)e.hpf;
        t.room     = (float)e.room;
        t.delay    = (float)e.delay;
        Audio::trigger(e.sample, t);
    }

    g_lastQueryTo = qEnd;
}

double currentCycle() { return g_curCycle; }

} // namespace Music
