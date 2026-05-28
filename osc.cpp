// osc.cpp — minimal OSC 1.0 UDP listener implementation.
//
// Design mirrors macOS/midi_coremidi.mm: a background thread fills a
// queue of FeedbackOscMsg, the render thread drains via
// feedback_osc_poll(). One UDP socket, one thread.
//
// OSC 1.0 wire format (per spec):
//   - Address: null-terminated string, padded to multiple of 4 bytes
//   - Type tag string: starts with ',', null-terminated, 4-byte padded
//     e.g. ",fi" means one float then one int
//   - Args: packed in tag order. 32-bit ints/floats are big-endian.
//     Strings are null-terminated + 4-byte padded.
//
// Bundles (#bundle\0 + timetag + size-prefixed messages) are detected
// and logged, but each contained message is NOT dispatched in v1 —
// TouchDesigner OSC Out CHOP sends individual messages, not bundles.

#include "osc.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
  #define CLOSESOCK closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <fcntl.h>
  using socket_t = int;
  static constexpr socket_t INVALID_SOCK = -1;
  #define CLOSESOCK ::close
#endif

namespace {

struct OscState {
    socket_t sock = INVALID_SOCK;
    int port = 0;
    std::atomic<bool> running{false};
    std::thread thr;
    std::mutex mu;
    std::deque<FeedbackOscMsg> q;
    std::mutex stateMu;
#ifdef _WIN32
    bool wsaUp = false;
#endif
};

OscState g_osc;

// Round up to next multiple of 4.
inline int pad4(int n) { return (n + 3) & ~3; }

// Big-endian 32-bit read; returns false on out-of-bounds.
bool read_be32(const uint8_t* buf, int len, int& off, uint32_t& out) {
    if (off + 4 > len) return false;
    out = ((uint32_t)buf[off]   << 24) |
          ((uint32_t)buf[off+1] << 16) |
          ((uint32_t)buf[off+2] << 8)  |
          ((uint32_t)buf[off+3]);
    off += 4;
    return true;
}

// Read a null-terminated OSC string at off, advance off past the
// padded boundary. Returns the start pointer (still in buf) or null
// on truncation. The string is in-place null-terminated already; we
// just verify it is and report its length.
const char* read_osc_string(const uint8_t* buf, int len, int& off, int& slen) {
    if (off >= len) return nullptr;
    int start = off;
    while (off < len && buf[off] != 0) off++;
    if (off >= len) return nullptr;        // no terminator found
    slen = off - start;
    // advance past the null and any padding
    int total = slen + 1;                  // include terminator
    int padded = pad4(total);
    off = start + padded;
    if (off > len) return nullptr;
    return reinterpret_cast<const char*>(buf + start);
}

// Parse one OSC message and push to the queue. Returns true if a
// dispatchable message was parsed (regardless of whether we recognized
// the type tag).
bool parse_message(const uint8_t* buf, int len) {
    int off = 0;
    int addr_len = 0;
    const char* addr = read_osc_string(buf, len, off, addr_len);
    if (!addr || addr[0] != '/') return false;

    int tag_len = 0;
    const char* tag = read_osc_string(buf, len, off, tag_len);
    // Some senders omit the type tag entirely. Treat as no-arg message.
    if (!tag || tag[0] != ',') {
        FeedbackOscMsg m = {};
        std::strncpy(m.address, addr, sizeof(m.address) - 1);
        m.arg_type = 0;
        std::lock_guard<std::mutex> lk(g_osc.mu);
        if (g_osc.q.size() >= 4096) g_osc.q.pop_front();
        g_osc.q.push_back(m);
        return true;
    }

    FeedbackOscMsg m = {};
    std::strncpy(m.address, addr, sizeof(m.address) - 1);
    m.arg_type = 0;
    m.arg_f = 0.0f;
    m.arg_i = 0;
    bool got_first = false;

    // Walk type chars after the ','.
    for (int t = 1; t < tag_len; t++) {
        char c = tag[t];
        if (c == 'i') {
            uint32_t raw;
            if (!read_be32(buf, len, off, raw)) return false;
            int32_t v = (int32_t)raw;
            if (!got_first) {
                m.arg_type = 'i';
                m.arg_i = v;
                m.arg_f = (float)v;
                got_first = true;
            }
        } else if (c == 'f') {
            uint32_t raw;
            if (!read_be32(buf, len, off, raw)) return false;
            float v;
            std::memcpy(&v, &raw, sizeof(v));
            if (!got_first) {
                m.arg_type = 'f';
                m.arg_f = v;
                m.arg_i = (int32_t)v;
                got_first = true;
            }
        } else if (c == 'T') {
            if (!got_first) {
                m.arg_type = 'T';
                m.arg_f = 1.0f;
                m.arg_i = 1;
                got_first = true;
            }
        } else if (c == 'F' || c == 'N' || c == 'I') {
            // F=false, N=nil, I=infinitum — treat all as 0/false-ish.
            if (!got_first) {
                m.arg_type = c;
                m.arg_f = 0.0f;
                m.arg_i = 0;
                got_first = true;
            }
        } else if (c == 's' || c == 'S') {
            int slen = 0;
            const char* s = read_osc_string(buf, len, off, slen);
            if (!s) return false;
            if (!got_first) {
                m.arg_type = 's';
                m.arg_f = 0.0f;
                m.arg_i = 0;
                got_first = true;
            }
        } else if (c == 'b') {
            // blob: int32 size + bytes + 4-byte pad
            uint32_t blob_len;
            if (!read_be32(buf, len, off, blob_len)) return false;
            int padded = pad4((int)blob_len);
            if (off + padded > len) return false;
            off += padded;
        } else {
            // Unknown tag char — bail rather than guess.
            return false;
        }
    }

    std::lock_guard<std::mutex> lk(g_osc.mu);
    if (g_osc.q.size() >= 4096) g_osc.q.pop_front();
    g_osc.q.push_back(m);
    return true;
}

// Bundle dispatch — parses element sizes and recurses into each.
// v1: we just walk and parse contained messages. Timetag is ignored.
void parse_bundle(const uint8_t* buf, int len) {
    // "#bundle\0" + 8-byte timetag = 16 bytes
    if (len < 16) return;
    int off = 16;
    while (off + 4 <= len) {
        uint32_t sz;
        if (!read_be32(buf, len, off, sz)) return;
        if ((int)sz < 0 || off + (int)sz > len) return;
        // contained element: either a nested bundle or a message
        if (sz >= 8 && std::memcmp(buf + off, "#bundle\0", 8) == 0) {
            parse_bundle(buf + off, (int)sz);
        } else {
            parse_message(buf + off, (int)sz);
        }
        off += (int)sz;
    }
}

void listener_thread() {
    uint8_t buf[2048];
    while (g_osc.running.load(std::memory_order_acquire)) {
        sockaddr_in src;
        socklen_t srclen = sizeof(src);
        int n = (int)recvfrom(g_osc.sock, (char*)buf, sizeof(buf), 0,
                              (sockaddr*)&src, &srclen);
        if (n <= 0) {
            // recvfrom returns -1 with EBADF when we close the socket
            // from the main thread; that's the normal shutdown path.
            if (!g_osc.running.load(std::memory_order_acquire)) break;
            continue;
        }
        if (n >= 8 && std::memcmp(buf, "#bundle\0", 8) == 0) {
            parse_bundle(buf, n);
        } else {
            parse_message(buf, n);
        }
    }
}

} // namespace

extern "C" int feedback_osc_open(int port) {
    std::lock_guard<std::mutex> lk(g_osc.stateMu);
    if (g_osc.sock != INVALID_SOCK && g_osc.port == port) return 1;
    if (g_osc.sock != INVALID_SOCK) {
        g_osc.running.store(false, std::memory_order_release);
        CLOSESOCK(g_osc.sock);
        g_osc.sock = INVALID_SOCK;
        if (g_osc.thr.joinable()) g_osc.thr.join();
    }

#ifdef _WIN32
    if (!g_osc.wsaUp) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::fprintf(stderr, "[osc] WSAStartup failed\n");
            return 0;
        }
        g_osc.wsaUp = true;
    }
#endif

    socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCK) {
        std::fprintf(stderr, "[osc] socket() failed\n");
        return 0;
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::fprintf(stderr, "[osc] bind to port %d failed\n", port);
        CLOSESOCK(s);
        return 0;
    }

    g_osc.sock = s;
    g_osc.port = port;
    g_osc.running.store(true, std::memory_order_release);
    g_osc.thr = std::thread(listener_thread);

    std::fprintf(stdout, "[osc] listening on UDP port %d\n", port);
    return 1;
}

extern "C" void feedback_osc_close(void) {
    std::lock_guard<std::mutex> lk(g_osc.stateMu);
    if (g_osc.sock == INVALID_SOCK) return;
    g_osc.running.store(false, std::memory_order_release);
    socket_t s = g_osc.sock;
    g_osc.sock = INVALID_SOCK;
    g_osc.port = 0;
    CLOSESOCK(s);
    if (g_osc.thr.joinable()) g_osc.thr.join();
    std::fprintf(stdout, "[osc] closed\n");
}

extern "C" int feedback_osc_poll(FeedbackOscMsg* out, int max_count) {
    if (!out || max_count <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_osc.mu);
    int n = 0;
    while (n < max_count && !g_osc.q.empty()) {
        out[n++] = g_osc.q.front();
        g_osc.q.pop_front();
    }
    return n;
}

extern "C" int feedback_osc_port(void) {
    std::lock_guard<std::mutex> lk(g_osc.stateMu);
    return g_osc.port;
}

extern "C" int feedback_osc_connected(void) {
    std::lock_guard<std::mutex> lk(g_osc.stateMu);
    return (g_osc.sock != INVALID_SOCK) ? 1 : 0;
}
