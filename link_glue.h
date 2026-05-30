// link_glue.h — minimal C interface around Ableton Link.
//
// Crutchfield's main loop and the existing MIDI Clock derivation in
// input.cpp talk to the Link layer through these extern "C" functions.
// Everything Link itself touches lives in link_glue.cpp so the heavy
// header (Link.hpp includes ASIO + a lot of metaprogramming) is in one
// translation unit.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Link with an initial tempo. Idempotent.
void  link_init(double initial_bpm);
void  link_shutdown(void);

// Enable / disable Link's network discovery. When disabled, the API
// still works locally but no peers can connect.
void  link_set_enabled(int on);
int   link_enabled(void);

// Current peer count on the local network.
int   link_num_peers(void);

// Read the network tempo (BPM). Smoothed over the last few seconds by
// Link itself.
double link_tempo(void);

// Set tempo. Propagates to all peers on the link session.
void   link_set_tempo(double bpm);

// Current beat phase within a quantum (e.g. 4 = bars of 4 beats).
// Returns a value in [0, quantum).
double link_beat_phase(double quantum);

// Has the beat clock crossed an integer-beat boundary since the last
// call? Returns 1 once per beat, 0 otherwise. Useful for triggering
// 1/N events from a polling main loop.
int    link_did_beat(double quantum);

// Reset the beat-edge detector (e.g. when transport starts).
void   link_reset_beat_edges(void);

// Is Link's start/stop sync currently in the "playing" state? Crutchfield
// reads this to mirror remote transport starts.
int    link_is_playing(void);
void   link_set_playing(int on);

#ifdef __cplusplus
}
#endif
