// music.h — embedded JavaScript runtime for pattern-driven audio.
//
// Step 1: just stand up QuickJS, register a print() function, run a
// "hello from JS" snippet at startup. Future steps add Strudel's pattern
// packages, a native sampler/synth, and a bridge exposing video-side
// scalars back into JS so patterns can react to what's on screen.
//
// We keep a single global Music instance; init() must be called once
// before any eval(). The audio thread, scheduler, and DSP all live
// behind this same abstraction so callers don't care about QuickJS.

#pragma once

#include <string>
#include <vector>

namespace Music {

// Boot the JS runtime. Idempotent — second calls are no-ops. Returns
// false on failure (prints to stderr).
bool init();

// Shut down the runtime and free everything. Safe to call even if
// init() failed or was never called.
void shutdown();

// Evaluate a chunk of JS source. `tag` is used in error messages to
// identify the source (e.g. a filename or "<startup>"). Returns true
// on success; prints any exception to stderr.
bool eval(const std::string& code, const std::string& tag);

// A single pattern event — what falls out of Pattern.queryArc mapped
// into a POD the rest of the app can consume.
struct Event {
    double      begin;     // cycle-time start (inclusive)
    double      end;       // cycle-time end (exclusive)
    std::string sample;    // value.s — e.g. "bd", "" if not set
    std::string note;      // value.note — e.g. "c3", "" if not set
    double      gain   = 1.0;
    double      pan    = 0.5;
    double      speed  = 1.0;
    double      lpf    = 0.0;
    double      hpf    = 0.0;
    double      room   = 0.0;
    double      delay  = 0.0;
    int         channel = 0;
};

// Evaluate a pattern expression (Strudel-syntax JS) and query events in
// cycle range [begin, end). Returns empty vector on error. `code` should
// be a single expression producing a Pattern — e.g. `s("bd sn hh*2")`.
std::vector<Event> query(const std::string& code, double begin, double end);

// ── Scheduler ────────────────────────────────────────────────────────
// Sets the active pattern (as a JS expression). Passing "" stops
// playback. Safe to call any time.
void setPattern(const std::string& code);
const std::string& pattern();

// Play / pause toggle. When paused, update() still advances the BPM
// clock internally so the scheduler picks up where it left off.
void setPlaying(bool on);
bool playing();

// Called once per frame from the main loop. `now` is the wall clock
// reading used for cycle-time computation. BPM is read fresh each frame
// so external tempo changes (MIDI clock, tap tempo) take effect live.
// Triggers audio events via the Audio module.
void update(double now, float bpm);

// Read-only state for the help UI.
double currentCycle();   // fractional cycle position (e.g. 3.75)

} // namespace Music
