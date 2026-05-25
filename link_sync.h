// link_sync.h — Ableton Link integration.
//
// Wraps ableton::Link in a thin C++ API. When enabled, Link joins a
// peer-to-peer network session; every participant shares a common tempo
// and phase so beat-driven visual effects (strobe, flash, hue-jump, …)
// lock to Ableton Live, Traktor, or any other Link-aware app on the LAN.
//
// Usage (main thread only):
//   LinkSync::init(p.bpm);           // start at current BPM
//   LinkSync::enable(true);          // join the session
//   …
//   if (LinkSync::isEnabled()) {
//       p.bpm   = (float)LinkSync::tempo();
//       phase   = (float)LinkSync::beatPhase();
//   }
//   LinkSync::shutdown();

#pragma once
#include <cstddef>

namespace LinkSync {

bool init(double initialBpm = 120.0);
void shutdown();

// Toggle network participation.
void enable(bool on);
bool isEnabled();

// Number of other Link-enabled apps currently on the session.
std::size_t peerCount();

// Current session tempo (BPM).
double tempo();

// Fractional position within the current beat: [0, 1).
// Two calls within the same frame read the same atomic snapshot.
double beatPhase();

// Push a new tempo to all Link peers. Call when local tap tempo changes
// so the network follows. No-op if Link is not enabled.
void setTempo(double bpm);

} // namespace LinkSync
