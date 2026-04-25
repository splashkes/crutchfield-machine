#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

struct FeedbackMidiMsg {
    uint8_t b[3];
    uint8_t len;
};

namespace {
struct CoreMidiState {
    MIDIClientRef client = 0;
    MIDIPortRef inputPort = 0;
    MIDIPortRef outputPort = 0;
    MIDIEndpointRef source = 0;
    MIDIEndpointRef destination = 0;
    std::string sourceName;
    std::string destinationName;
    bool connected = false;
    bool outputConnected = false;
    bool initialized = false;
    std::mutex stateMu;
    std::mutex mu;
    std::deque<FeedbackMidiMsg> q;

    uint8_t runningStatus = 0;
    uint8_t msg[3] = {};
    uint8_t count = 0;
    uint8_t need = 0;
    bool inSysex = false;
};

CoreMidiState g_midi;

static std::string cf_string_to_utf8(CFStringRef s) {
    if (!s) return {};
    char buf[256];
    if (CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) {
        return buf;
    }
    CFIndex len = CFStringGetLength(s);
    CFIndex cap = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out((size_t)cap, '\0');
    if (CFStringGetCString(s, out.data(), cap, kCFStringEncodingUTF8)) {
        out.resize(std::strlen(out.c_str()));
        return out;
    }
    return {};
}

static std::string endpoint_name(MIDIEndpointRef endpoint) {
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name) != noErr || !name) {
        MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name);
    }
    std::string out = cf_string_to_utf8(name);
    if (name) CFRelease(name);
    return out;
}

static bool contains_ci(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto lower = [](unsigned char c) { return (char)std::tolower(c); };
    std::string h, n;
    h.reserve(hay.size());
    n.reserve(needle.size());
    for (unsigned char c : hay) h.push_back(lower(c));
    for (unsigned char c : needle) n.push_back(lower(c));
    return h.find(n) != std::string::npos;
}

static uint8_t message_len(uint8_t status) {
    if (status >= 0xF8) return 1;
    if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) return 2;
    if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0) return 3;
    switch (status) {
        case 0xF1:
        case 0xF3: return 2;
        case 0xF2: return 3;
        default: return 0;
    }
}

static void enqueue_msg(const uint8_t* bytes, uint8_t len) {
    if (len == 0 || len > 3) return;
    FeedbackMidiMsg m = {};
    m.len = len;
    for (uint8_t i = 0; i < len; i++) m.b[i] = bytes[i];
    std::lock_guard<std::mutex> lk(g_midi.mu);
    if (g_midi.q.size() >= 4096) g_midi.q.pop_front();
    g_midi.q.push_back(m);
}

static void parse_byte(uint8_t byte) {
    if (byte >= 0xF8) {
        uint8_t msg[1] = { byte };
        enqueue_msg(msg, 1);
        return;
    }
    if (byte == 0xF0) {
        g_midi.inSysex = true;
        g_midi.count = 0;
        g_midi.need = 0;
        return;
    }
    if (g_midi.inSysex) {
        if (byte == 0xF7) g_midi.inSysex = false;
        return;
    }
    if (byte & 0x80) {
        g_midi.runningStatus = byte;
        g_midi.need = message_len(byte);
        g_midi.count = 0;
        if (g_midi.need == 0) return;
        g_midi.msg[g_midi.count++] = byte;
        if (g_midi.need == 1) {
            enqueue_msg(g_midi.msg, 1);
            g_midi.count = 0;
        }
        return;
    }

    if (g_midi.count == 0) {
        if (!g_midi.runningStatus) return;
        g_midi.need = message_len(g_midi.runningStatus);
        if (g_midi.need == 0) return;
        g_midi.msg[0] = g_midi.runningStatus;
        g_midi.count = 1;
    }
    if (g_midi.count < 3) g_midi.msg[g_midi.count++] = byte & 0x7F;
    if (g_midi.need > 0 && g_midi.count >= g_midi.need) {
        enqueue_msg(g_midi.msg, g_midi.need);
        g_midi.count = 0;
    }
}

static void midi_read_proc(const MIDIPacketList* pktlist, void*, void*) {
    const MIDIPacket* pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        for (UInt16 j = 0; j < pkt->length; j++) {
            parse_byte(pkt->data[j]);
        }
        pkt = MIDIPacketNext(pkt);
    }
}

static void midi_notify_proc(const MIDINotification* msg, void*) {
    if (!msg) return;
    switch (msg->messageID) {
        case kMIDIMsgObjectAdded:
        case kMIDIMsgObjectRemoved:
        case kMIDIMsgSetupChanged:
        {
            std::lock_guard<std::mutex> lk(g_midi.stateMu);
            if (g_midi.connected || g_midi.outputConnected) {
                std::fprintf(stderr, "[midi] device change; reconnecting MIDI endpoints\n");
            }
            g_midi.connected = false;
            g_midi.outputConnected = false;
            g_midi.source = 0;
            g_midi.destination = 0;
            g_midi.sourceName.clear();
            g_midi.destinationName.clear();
            g_midi.runningStatus = 0;
            g_midi.count = 0;
            g_midi.need = 0;
            g_midi.inSysex = false;
            break;
        }
        default:
            break;
    }
}
}

extern "C" int feedback_midi_open(const char* port_hint) {
    std::lock_guard<std::mutex> lk(g_midi.stateMu);
    if (g_midi.connected && g_midi.outputConnected) return 1;

    if (!g_midi.initialized) {
        OSStatus err = MIDIClientCreate(CFSTR("Crutchfield Machine"), midi_notify_proc, nullptr,
                                        &g_midi.client);
        if (err != noErr) {
            std::fprintf(stderr, "[midi] CoreMIDI client create failed: %d\n", (int)err);
            return 0;
        }
        err = MIDIInputPortCreate(g_midi.client, CFSTR("Crutchfield Machine Input"),
                                  midi_read_proc, nullptr, &g_midi.inputPort);
        if (err != noErr) {
            std::fprintf(stderr, "[midi] CoreMIDI input port create failed: %d\n", (int)err);
            return 0;
        }
        err = MIDIOutputPortCreate(g_midi.client, CFSTR("Crutchfield Machine Output"),
                                   &g_midi.outputPort);
        if (err != noErr) {
            std::fprintf(stderr, "[midi] CoreMIDI output port create failed: %d\n", (int)err);
            g_midi.outputPort = 0;
        }
        g_midi.initialized = true;
    }

    std::string hint = port_hint ? port_hint : "";

    if (!g_midi.connected) {
        MIDIEndpointRef fallback = 0;
        std::string fallbackName;
        ItemCount n = MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < n; i++) {
            MIDIEndpointRef src = MIDIGetSource(i);
            std::string name = endpoint_name(src);
            if (name.empty()) continue;
            std::fprintf(stdout, "[midi] source: %s\n", name.c_str());
            if (fallback == 0 || contains_ci(name, "DDJ-FLX2") || contains_ci(name, "DDJ")) {
                fallback = src;
                fallbackName = name;
            }
            if (!hint.empty() && contains_ci(name, hint)) {
                fallback = src;
                fallbackName = name;
                break;
            }
        }

        if (!fallback) {
            static bool warnedNoSources = false;
            if (!warnedNoSources) {
                std::fprintf(stderr, "[midi] no CoreMIDI sources found\n");
                warnedNoSources = true;
            }
        } else {
            OSStatus err = MIDIPortConnectSource(g_midi.inputPort, fallback, nullptr);
            if (err != noErr) {
                std::fprintf(stderr, "[midi] failed to connect '%s': %d\n",
                             fallbackName.c_str(), (int)err);
            } else {
                g_midi.source = fallback;
                g_midi.sourceName = fallbackName;
                g_midi.connected = true;
                std::fprintf(stdout, "[midi] CoreMIDI opened '%s'\n", fallbackName.c_str());
            }
        }
    }

    if (!g_midi.outputConnected) {
        MIDIEndpointRef outFallback = 0;
        std::string outFallbackName;
        ItemCount nd = MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < nd; i++) {
            MIDIEndpointRef dst = MIDIGetDestination(i);
            std::string name = endpoint_name(dst);
            if (name.empty()) continue;
            std::fprintf(stdout, "[midi] destination: %s\n", name.c_str());
            if (outFallback == 0 || contains_ci(name, "DDJ-FLX2") || contains_ci(name, "DDJ")) {
                outFallback = dst;
                outFallbackName = name;
            }
            if (!hint.empty() && contains_ci(name, hint)) {
                outFallback = dst;
                outFallbackName = name;
                break;
            }
        }

        if (outFallback && g_midi.outputPort) {
            g_midi.destination = outFallback;
            g_midi.destinationName = outFallbackName;
            g_midi.outputConnected = true;
            std::fprintf(stdout, "[midi] CoreMIDI output '%s'\n", outFallbackName.c_str());
        } else {
            static bool warnedNoDest = false;
            if (!warnedNoDest) {
                std::fprintf(stderr, "[midi] no CoreMIDI destination found for LED feedback\n");
                warnedNoDest = true;
            }
        }
    }

    return g_midi.connected ? 1 : 0;
}

extern "C" int feedback_midi_poll(FeedbackMidiMsg* out, int max_count) {
    if (!out || max_count <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_midi.mu);
    int n = 0;
    while (n < max_count && !g_midi.q.empty()) {
        out[n++] = g_midi.q.front();
        g_midi.q.pop_front();
    }
    return n;
}

extern "C" void feedback_midi_status(char* name, int name_cap, int* connected) {
    std::lock_guard<std::mutex> lk(g_midi.stateMu);
    if (connected) *connected = g_midi.connected ? 1 : 0;
    if (name && name_cap > 0) {
        name[0] = '\0';
        if (g_midi.connected) {
            std::snprintf(name, (size_t)name_cap, "%s", g_midi.sourceName.c_str());
        }
    }
}

extern "C" int feedback_midi_send_note(int channel, int note, int velocity) {
    if (channel < 1 || channel > 16 || note < 0 || note > 127) return 0;
    if (velocity < 0) velocity = 0;
    if (velocity > 127) velocity = 127;

    MIDIPortRef outputPort = 0;
    MIDIEndpointRef destination = 0;
    {
        std::lock_guard<std::mutex> lk(g_midi.stateMu);
        if (!g_midi.outputConnected || !g_midi.outputPort || !g_midi.destination) return 0;
        outputPort = g_midi.outputPort;
        destination = g_midi.destination;
    }

    Byte buffer[128] = {};
    MIDIPacketList* packets = reinterpret_cast<MIDIPacketList*>(buffer);
    MIDIPacket* packet = MIDIPacketListInit(packets);
    Byte msg[3] = {
        static_cast<Byte>(0x90 | ((channel - 1) & 0x0F)),
        static_cast<Byte>(note & 0x7F),
        static_cast<Byte>(velocity & 0x7F),
    };
    packet = MIDIPacketListAdd(packets, sizeof buffer, packet, 0, 3, msg);
    if (!packet) return 0;
    return MIDISend(outputPort, destination, packets) == noErr ? 1 : 0;
}
