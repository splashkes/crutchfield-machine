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
#include <filesystem>
#include <chrono>
#include <vector>
#include <algorithm>

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

    // Video ↔ music bridge — create a `fb` global object patterns can
    // read scalars from. setScalar() mutates properties on this object;
    // JS sees the updated values on the next queryArc.
    {
        JSValue g = JS_GetGlobalObject(g_ctx);
        JS_SetPropertyStr(g_ctx, g, "fb", JS_NewObject(g_ctx));
        JS_FreeValue(g_ctx, g);
    }

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

        JSValue part  = JS_GetPropertyStr(g_ctx, h, "part");
        JSValue whole = JS_GetPropertyStr(g_ctx, h, "whole");
        JSValue val   = JS_GetPropertyStr(g_ctx, h, "value");

        Event e;
        e.begin      = get_num(g_ctx, part,  "begin", 0.0);
        e.end        = get_num(g_ctx, part,  "end",   0.0);
        e.wholeBegin = get_num(g_ctx, whole, "begin", e.begin);
        e.wholeEnd   = get_num(g_ctx, whole, "end",   e.end);
        e.sample     = get_str(g_ctx, val,   "s");
        e.note       = get_str(g_ctx, val,   "note");
        e.gain    = get_num(g_ctx, val,  "gain",   1.0);
        e.pan     = get_num(g_ctx, val,  "pan",    0.5);
        e.speed   = get_num(g_ctx, val,  "speed",  1.0);
        e.lpf     = get_num(g_ctx, val,  "lpf",    0.0);
        e.hpf     = get_num(g_ctx, val,  "hpf",    0.0);
        e.room    = get_num(g_ctx, val,  "room",   0.0);
        e.delay   = get_num(g_ctx, val,  "delay",  0.0);
        e.channel = (int)get_num(g_ctx, val, "channel", 0);
        e.attack  = get_num(g_ctx, val, "attack",  0.0);
        e.decayT  = get_num(g_ctx, val, "decayT",  0.0);
        e.sustain = get_num(g_ctx, val, "sustain", -1.0);
        e.release = get_num(g_ctx, val, "release", 0.0);
        out.push_back(std::move(e));

        JS_FreeValue(g_ctx, part);
        JS_FreeValue(g_ctx, whole);
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
    double      g_lastQueryTo = 0.0;   // cycle-time up to which we've fired
    double      g_lastBpm     = 120.0;
    double      g_curCycle    = 0.0;   // advanced by update() each frame
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
        // Pick up at the current cycle time so a reload doesn't trigger
        // a flood of back-scheduled events from cycle 0 onward.
        g_lastQueryTo = g_curCycle;
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

void update(double now, float dt, float bpm) {
    if (!g_up) return;
    (void)now;

    const double cs = cycle_seconds(bpm);
    // Clamp per-frame dt so giant stalls (alt-tab, debugger pause, the
    // app being obscured and Windows throttling the render thread) can't
    // dump a pile of scheduled events when execution resumes. 100 ms
    // max per frame ≈ 0.05 cycles at 120 BPM, well inside the lookahead.
    float stepDt = dt;
    if (stepDt < 0.f)    stepDt = 0.f;
    if (stepDt > 0.1f)   stepDt = 0.1f;
    g_curCycle += stepDt / cs;
    g_lastBpm   = bpm;

    if (!g_playing || g_pattern.empty()) return;

    // If the update loop has been starved for more than a cycle (focus
    // loss, stutter, debugger pause), snap the query cursor forward
    // instead of trying to replay all missed events in bulk — that
    // would dump a pile of triggers with delaySec clamped to 0.
    if (g_curCycle - g_lastQueryTo > 1.0) {
        g_lastQueryTo = g_curCycle;
    }

    // Query window: from last-fired up to now + lookahead.
    double qStart = g_lastQueryTo;
    double qEnd   = g_curCycle + kLookahead;
    if (qEnd <= qStart) return;

    auto evs = query(g_pattern, qStart, qEnd);
    // Optional per-trigger log so we can sanity-check scheduling from
    // the console without wiring a full HUD view. Enable with
    //   setx FEEDBACK_MUSIC_DEBUG 1  (then restart the app)
    static int s_dbg = -1;
    if (s_dbg < 0) {
        const char* e = std::getenv("FEEDBACK_MUSIC_DEBUG");
        s_dbg = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    for (const auto& e : evs) {
        // Only fire events whose natural onset (whole.begin) is inside
        // THIS query slice. queryArc returns every hap whose part
        // overlaps the arc — firing on part.begin would re-trigger every
        // frame as the query window slid across a long hap.
        if (e.wholeBegin < qStart || e.wholeBegin >= qEnd) continue;

        double delaySec = (e.wholeBegin - g_curCycle) * cs;
        if (delaySec < 0.0) delaySec = 0.0;
        if (s_dbg) {
            std::fprintf(stderr,
                "[sched] cyc=%.3f wbeg=%.3f dly=%.3fs s=%-6s note=%-4s\n",
                g_curCycle, e.wholeBegin, delaySec,
                e.sample.empty() ? "-" : e.sample.c_str(),
                e.note.empty() ? "-" : e.note.c_str());
        }

        // Branch on what's attached to the hap:
        //   • note set (with or without s=saw/square/…) → synth voice
        //   • s set to a sample name → sample trigger
        //   • nothing → ignore
        auto fill_note = [&](Audio::NoteOpts& n) {
            n.delaySec    = delaySec;
            n.durationSec = (float)((e.wholeEnd - e.wholeBegin) * cs);
            n.gain        = (float)e.gain * 0.35f;
            n.pan         = (float)e.pan;
            n.lpf         = (float)e.lpf;
            n.hpf         = (float)e.hpf;
            n.delaySend   = (float)e.delay;
            n.roomSend    = (float)e.room;
            if (e.attack  > 0.0) n.attack  = (float)e.attack;
            if (e.decayT  > 0.0) n.decay   = (float)e.decayT;
            if (e.sustain >= 0.0) n.sustain = (float)e.sustain;
            if (e.release > 0.0) n.release = (float)e.release;
        };

        if (!e.note.empty()) {
            int midi = Audio::note_to_midi(e.note);
            if (midi < 0) continue;
            Audio::NoteOpts n;
            n.freqHz = Audio::midi_to_freq(midi);
            n.wave   = Audio::is_synth_name(e.sample)
                     ? Audio::waveform_from_name(e.sample)
                     : Audio::WAVE_SAW;
            fill_note(n);
            Audio::trigger_note(n);
            continue;
        }

        if (e.sample.empty()) continue;

        if (Audio::is_synth_name(e.sample)) {
            Audio::NoteOpts n;
            n.wave = Audio::waveform_from_name(e.sample);
            fill_note(n);
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

void setScalar(const char* name, double value) {
    if (!g_up || !name) return;
    JSValue g  = JS_GetGlobalObject(g_ctx);
    JSValue fb = JS_GetPropertyStr(g_ctx, g, "fb");
    if (!JS_IsObject(fb)) {
        JS_FreeValue(g_ctx, fb);
        fb = JS_NewObject(g_ctx);
        // Install the newly-created object (and also drop the reference we
        // just made by passing it into SetPropertyStr, which takes ownership).
        JS_SetPropertyStr(g_ctx, g, "fb", JS_DupValue(g_ctx, fb));
    }
    JS_SetPropertyStr(g_ctx, fb, name, JS_NewFloat64(g_ctx, value));
    JS_FreeValue(g_ctx, fb);
    JS_FreeValue(g_ctx, g);
}

// ── Preset system ────────────────────────────────────────────────────

namespace {
    std::vector<std::string> g_presetPaths;    // absolute paths
    int                      g_presetIdx = -1;
    std::string              g_presetName;
    std::filesystem::file_time_type g_presetMtime;
    double                   g_nextReloadCheck = 0.0;
    int                      g_momentaryBase  = -1;   // preset idx to restore on pop
    bool                     g_inMomentary    = false;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int scanPresets(const std::string& dir) {
    namespace fs = std::filesystem;
    g_presetPaths.clear();
    if (!fs::is_directory(dir)) return 0;
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (!ent.is_regular_file()) continue;
        fs::path p = ent.path();
        std::string ext = p.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".strudel" && ext != ".js") continue;
        g_presetPaths.push_back(p.string());
    }
    std::sort(g_presetPaths.begin(), g_presetPaths.end());
    std::fprintf(stdout, "[music] %zu preset(s) in %s\n",
                 g_presetPaths.size(), dir.c_str());
    return (int)g_presetPaths.size();
}

int presetCount() { return (int)g_presetPaths.size(); }

bool loadPreset(int index) {
    namespace fs = std::filesystem;
    if (g_presetPaths.empty()) return false;
    int n = (int)g_presetPaths.size();
    index = ((index % n) + n) % n;
    const std::string& path = g_presetPaths[index];
    std::string code = read_file(path);
    if (code.empty()) {
        std::fprintf(stderr, "[music] preset %s is empty or unreadable\n", path.c_str());
        return false;
    }
    g_presetIdx   = index;
    g_presetName  = fs::path(path).stem().string();
    // Skip line comments for cleanliness — Strudel code often includes
    // `//` comments that we want to forward to the JS engine intact;
    // but for safety we trim trailing whitespace so one-liner files
    // don't trip the parser with stray newlines.
    setPattern(code);
    g_presetMtime = fs::last_write_time(path);
    std::fprintf(stdout, "[music] loaded preset '%s' [%d/%d]\n",
                 g_presetName.c_str(), index + 1, n);
    return true;
}

void nextPreset() {
    if (g_presetPaths.empty()) return;
    int i = (g_presetIdx < 0) ? 0 : ((g_presetIdx + 1) % (int)g_presetPaths.size());
    loadPreset(i);
}

void prevPreset() {
    if (g_presetPaths.empty()) return;
    int n = (int)g_presetPaths.size();
    int i = (g_presetIdx < 0) ? (n - 1) : (((g_presetIdx - 1) % n + n) % n);
    loadPreset(i);
}

const std::string& currentPresetName() { return g_presetName; }

void pushMomentaryPreset(const std::string& nameSubstr) {
    namespace fs = std::filesystem;
    if (g_inMomentary) return;                 // already jumped — ignore
    if (g_presetPaths.empty()) return;
    // Find the first preset whose stem contains the substring (case-ins).
    auto lc = [](std::string s){
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    std::string needle = lc(nameSubstr);
    int target = -1;
    for (int i = 0; i < (int)g_presetPaths.size(); i++) {
        std::string stem = lc(fs::path(g_presetPaths[i]).stem().string());
        if (stem.find(needle) != std::string::npos) { target = i; break; }
    }
    if (target < 0 || target == g_presetIdx) return;
    g_momentaryBase = g_presetIdx;
    g_inMomentary   = true;
    loadPreset(target);
}

void popMomentaryPreset() {
    if (!g_inMomentary) return;
    g_inMomentary = false;
    int restore = g_momentaryBase;
    g_momentaryBase = -1;
    if (restore >= 0 && restore < (int)g_presetPaths.size() && restore != g_presetIdx) {
        loadPreset(restore);
    }
}

void pollPresetReload() {
    namespace fs = std::filesystem;
    if (g_presetIdx < 0 || g_presetIdx >= (int)g_presetPaths.size()) return;
    // Throttle: check once every ~250ms.
    double now = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() * 0.001;
    if (now < g_nextReloadCheck) return;
    g_nextReloadCheck = now + 0.25;

    const std::string& path = g_presetPaths[g_presetIdx];
    std::error_code ec;
    auto mt = fs::last_write_time(path, ec);
    if (ec) return;
    if (mt != g_presetMtime) {
        std::string code = read_file(path);
        if (code.empty()) return;
        g_presetMtime = mt;
        setPattern(code);
        std::fprintf(stdout, "[music] hot-reloaded '%s'\n", g_presetName.c_str());
    }
}

} // namespace Music
