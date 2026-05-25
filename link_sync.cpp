// link_sync.cpp — Ableton Link backend.
// Compiled with -DLINK_PLATFORM_MACOSX (set in Makefile) so the Link
// headers select the Darwin clock (mach_absolute_time) and ASIO-based
// networking. No extra macOS frameworks required beyond what the host
// already links (-framework Foundation is sufficient).

#include "link_sync.h"
#include <ableton/Link.hpp>
#include <memory>

namespace {

struct State {
    ableton::Link link;
    explicit State(double bpm) : link(bpm) {}
};

std::unique_ptr<State> g_state;

} // namespace

namespace LinkSync {

bool init(double initialBpm) {
    g_state = std::make_unique<State>(initialBpm);
    return true;
}

void shutdown() { g_state.reset(); }

void enable(bool on) {
    if (g_state) g_state->link.enable(on);
}

bool isEnabled() {
    return g_state && g_state->link.isEnabled();
}

std::size_t peerCount() {
    return g_state ? g_state->link.numPeers() : 0;
}

double tempo() {
    if (!g_state) return 120.0;
    return g_state->link.captureAppSessionState().tempo();
}

double beatPhase() {
    if (!g_state) return 0.0;
    auto t     = g_state->link.clock().micros();
    auto state = g_state->link.captureAppSessionState();
    return state.phaseAtTime(t, 1.0);
}

void setTempo(double bpm) {
    if (!g_state || !g_state->link.isEnabled()) return;
    auto t     = g_state->link.clock().micros();
    auto state = g_state->link.captureAppSessionState();
    state.setTempo(bpm, t);
    g_state->link.commitAppSessionState(state);
}

} // namespace LinkSync
