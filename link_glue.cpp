// link_glue.cpp — implementation of the Ableton Link bridge.
//
// We hide the Link.hpp include in this single translation unit. The
// header brings in ASIO, std::variant, std::optional, and a fair
// amount of template metaprogramming; isolating it here keeps build
// times sane for the rest of the project.
//
// Build-time defines (set by the Makefile):
//   LINK_PLATFORM_MACOSX or LINK_PLATFORM_LINUX or LINK_PLATFORM_WINDOWS
//   LINK_PLATFORM_UNIX (for posix variants)
// Plus -Ivendor/link/include -Ivendor/link/modules/asio-standalone/asio/include.
//
// If vendor/link isn't present (e.g. fresh clone without submodule
// init), this file compiles as a no-op stub so the rest of the project
// still builds. Every link.* binding silently does nothing in that case.

#include "link_glue.h"

#if !defined(__has_include)
  // GCC < 5 / MSVC: assume Link is not present.
  #define LINK_GLUE_HAS_LINK 0
#elif __has_include(<ableton/Link.hpp>)
  #define LINK_GLUE_HAS_LINK 1
#else
  #define LINK_GLUE_HAS_LINK 0
#endif

#include <cstdio>

#if !LINK_GLUE_HAS_LINK

// ── Stub implementation ─────────────────────────────────────────────
// Compiled when <ableton/Link.hpp> isn't reachable. Every entry point
// is a safe no-op. The host can still call link_init() etc; nothing
// happens but nothing crashes.

extern "C" void   link_init(double)              { /* no-op */ }
extern "C" void   link_shutdown(void)            { /* no-op */ }
extern "C" void   link_set_enabled(int)          { /* no-op */ }
extern "C" int    link_enabled(void)             { return 0; }
extern "C" int    link_num_peers(void)           { return 0; }
extern "C" double link_tempo(void)               { return 120.0; }
extern "C" void   link_set_tempo(double)         { /* no-op */ }
extern "C" double link_beat_phase(double)        { return 0.0; }
extern "C" int    link_did_beat(double)          { return 0; }
extern "C" void   link_reset_beat_edges(void)    { /* no-op */ }
extern "C" int    link_is_playing(void)          { return 0; }
extern "C" void   link_set_playing(int)          { /* no-op */ }

#else  // LINK_GLUE_HAS_LINK

#include <ableton/Link.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace {

struct LinkState {
    std::unique_ptr<ableton::Link> link;
    std::mutex mu;
    std::atomic<bool> playingMirror{false};

    // Beat-edge detector state. lastBeat is the last reported beat (as
    // a double) seen by link_did_beat(); did_beat() returns 1 when the
    // integer-beat value advances. This lets the polling main loop
    // detect once-per-beat events without us having to dispatch on the
    // audio thread.
    double lastBeat = -1.0;
};

LinkState g_link;

} // namespace

extern "C" void link_init(double initial_bpm) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (g_link.link) return;
    if (initial_bpm <= 0.0) initial_bpm = 120.0;
    g_link.link = std::make_unique<ableton::Link>(initial_bpm);
    g_link.link->enableStartStopSync(true);
    g_link.link->setTempoCallback([](double) {});
    g_link.link->setStartStopCallback([](bool on) {
        g_link.playingMirror.store(on, std::memory_order_release);
    });
}

extern "C" void link_shutdown(void) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    g_link.link.reset();
}

extern "C" void link_set_enabled(int on) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return;
    g_link.link->enable(on != 0);
}

extern "C" int link_enabled(void) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return 0;
    return g_link.link->isEnabled() ? 1 : 0;
}

extern "C" int link_num_peers(void) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return 0;
    return (int)g_link.link->numPeers();
}

extern "C" double link_tempo(void) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return 120.0;
    return g_link.link->captureAppSessionState().tempo();
}

extern "C" void link_set_tempo(double bpm) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link || bpm <= 0.0 || bpm > 999.0) return;
    auto state = g_link.link->captureAppSessionState();
    state.setTempo(bpm, g_link.link->clock().micros());
    g_link.link->commitAppSessionState(state);
}

extern "C" double link_beat_phase(double quantum) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return 0.0;
    if (quantum <= 0.0) quantum = 4.0;
    auto state = g_link.link->captureAppSessionState();
    double phase = state.phaseAtTime(g_link.link->clock().micros(), quantum);
    if (phase < 0.0) phase += quantum;
    return phase;
}

extern "C" int link_did_beat(double quantum) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return 0;
    if (quantum <= 0.0) quantum = 4.0;
    auto state = g_link.link->captureAppSessionState();
    double beat = state.beatAtTime(g_link.link->clock().micros(), quantum);
    if (g_link.lastBeat < 0.0) { g_link.lastBeat = beat; return 0; }
    int crossings = (int)std::floor(beat) - (int)std::floor(g_link.lastBeat);
    g_link.lastBeat = beat;
    return crossings > 0 ? 1 : 0;
}

extern "C" void link_reset_beat_edges(void) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    g_link.lastBeat = -1.0;
}

extern "C" int link_is_playing(void) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return 0;
    return g_link.link->captureAppSessionState().isPlaying() ? 1 : 0;
}

extern "C" void link_set_playing(int on) {
    std::lock_guard<std::mutex> lk(g_link.mu);
    if (!g_link.link) return;
    auto state = g_link.link->captureAppSessionState();
    state.setIsPlaying(on != 0, g_link.link->clock().micros());
    g_link.link->commitAppSessionState(state);
}

#endif  // LINK_GLUE_HAS_LINK
