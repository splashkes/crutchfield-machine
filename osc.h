// osc.h — minimal OSC 1.0 UDP listener for the feedback app.
//
// Mirrors the macOS/midi_coremidi.mm extern "C" interface: a background
// thread receives UDP datagrams, parses them into FeedbackOscMsg, and
// queues them for the render thread to drain via feedback_osc_poll().
//
// No external deps. POSIX sockets on Apple/Linux. Windows uses winsock
// via the same interface (osc.cpp #ifdef wraps the differences).
//
// Supported OSC types in v1: i (int32), f (float32), T (true), F (false).
// String args (s) are parsed and skipped. Bundles (#bundle) are logged
// and skipped. Address patterns are matched literally (no wildcards).

#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// One parsed OSC message ready for dispatch. Address is null-terminated.
// arg_f holds the first numeric argument as a float (ints are cast). If
// the type tag is T/F, arg_f is 1.0/0.0 respectively. Type tag char is
// preserved in arg_type for callers that need it.
struct FeedbackOscMsg {
    char    address[128];
    float   arg_f;
    int32_t arg_i;
    char    arg_type;   // 'f', 'i', 'T', 'F', 's', or 0 if no args
    uint8_t reserved[3];
};

// Open a UDP socket bound to the given port and start the listener
// thread. Returns 1 on success, 0 on failure. Re-calling with the same
// port is a no-op success. Calling with a different port closes the
// previous socket first.
int  feedback_osc_open(int port);

// Close the listener and drop the socket. Safe to call when not open.
void feedback_osc_close(void);

// Drain up to max_count messages from the queue into out[]. Returns the
// number of messages copied. Non-blocking. Call once per frame on the
// render thread, exactly like feedback_midi_poll.
int  feedback_osc_poll(struct FeedbackOscMsg* out, int max_count);

// Get the current bound port (or 0 if not open).
int  feedback_osc_port(void);

// Returns 1 if the socket is open and the thread is running, else 0.
int  feedback_osc_connected(void);

// Configure outbound OSC echo. Pass host_or_null = NULL or port = 0 to
// disable. Single sink. Subsequent calls replace the previous config.
// Returns 1 on success (resolved + ready to send), 0 on failure.
int  feedback_osc_set_echo(const char* host_or_null, int port);

// Send one OSC message to the currently-configured echo sink. Returns
// 1 on success, 0 if echo is not configured or send failed. Safe to
// call from any thread. value_type: 'f' (float), 'i' (int), 'T' (true,
// no payload), 'F' (false, no payload).
int  feedback_osc_send_f(const char* address, float value);
int  feedback_osc_send_i(const char* address, int32_t value);
int  feedback_osc_send_bang(const char* address);  // type tag ",T"

#ifdef __cplusplus
}
#endif
