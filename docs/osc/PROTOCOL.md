# OSC Protocol — wire format & parser behavior

What goes over the wire, what we accept, what we ignore, and how to debug at the byte level.

## OSC 1.0 wire format (the part we use)

A datagram is one of:

- **Message**: `<address> <type-tag> <args>...`
- **Bundle**: `#bundle\0 <timetag> <size><element> <size><element>...`

All strings are null-terminated and padded with zeros to a multiple of 4 bytes. All numeric types are 4-byte big-endian (network byte order). The official spec is at <https://opensoundcontrol.stanford.edu/spec-1_0.html>.

### A message in bytes

For `/cma/decay  f  0.85`:

```
offset  hex                                  meaning
─────────────────────────────────────────────────────────────────────
00      2F 63 6D 61 2F 64 65 63 61 79 00 00  "/cma/decay\0" + 2 bytes pad
                                              (10 chars + null = 11, padded to 12)
0C      2C 66 00 00                          ",f\0" + 1 byte pad
                                              (3 chars including ',', padded to 4)
10      3F 59 99 9A                          0.85 as IEEE 754 big-endian

Total: 20 bytes
```

The leading `,` on the type tag is required by the spec. If it's missing, we treat the message as having no args (still dispatchable as a "bang" trigger).

### A bundle in bytes

For `#bundle [now] /cma/decay f 0.85 /cma/sat f 0.3`:

```
offset  hex                                  meaning
─────────────────────────────────────────────────────────────────────
00      23 62 75 6E 64 6C 65 00              "#bundle\0"
08      00 00 00 00 00 00 00 01              timetag (immediate = 1)
10      00 00 00 14                          first element size (20 bytes)
14      [20 bytes of /cma/decay message]
28      00 00 00 14                          second element size (20 bytes)
2C      [20 bytes of /cma/sat message]

Total: 64 bytes
```

We recursively descend into bundles and dispatch each contained message individually. The timetag is parsed but **ignored** — every contained message is dispatched immediately. (OSC 1.0 timetags are meant for scheduling but we have no use case for forward-dated dispatch.)

## Type tag characters

Each character after the leading `,` in the type tag string describes one argument. We support:

| Tag | OSC name | Bytes on wire | Our handling | Notes |
| --- | --- | --- | --- | --- |
| `f` | float32 | 4 (big-endian IEEE 754) | parsed → `arg_f` | the workhorse |
| `i` | int32 | 4 (big-endian) | parsed → `arg_i`, cast to `arg_f` | useful for counters, indices |
| `T` | true | 0 (no payload) | `arg_f = 1.0` | one-arg trigger pattern |
| `F` | false | 0 (no payload) | `arg_f = 0.0` | trigger release |
| `N` | nil | 0 | `arg_f = 0.0` | rare; treat as false |
| `I` | infinitum | 0 | `arg_f = 0.0` | rare; treat as false |
| `s` | string | null-term + 4-byte pad | parsed, **first arg ignored** | logged in `--osc-learn` |
| `S` | symbol | same as `s` | parsed, ignored | OSC 1.1 alias for string |
| `b` | blob | int32 size + bytes + pad | parsed and **skipped** | binary data; no use yet |

**Important**: only the **first argument** of any message is consumed for dispatch. Later args are parsed (for offset correctness) and discarded. This is intentional — one OSC address = one parameter. If you need to drive multiple parameters, send multiple messages.

### What happens with unsupported tags

Any tag character not in the table above (`h`, `t`, `d`, `r`, `c`, `m`, `[`, `]`, …) causes the parser to **bail on the entire message**. The message is not enqueued. This is conservative: an unknown tag could mean an unknown byte count, and guessing risks reading past the buffer or de-syncing the parser on later args. The trade-off: messages with unfamiliar args are silently dropped.

### Address requirements

Addresses must:

- Start with `/`
- Be null-terminated
- Be valid UTF-8 (not enforced but we don't transcode)

Addresses up to 127 bytes are accepted; the 128-byte buffer in `FeedbackOscMsg::address` includes the trailing `\0`. Longer addresses are silently truncated to fit.

We do **not** implement OSC address pattern matching (`?`, `*`, `[]`, `{}`, `//`). Bindings match by literal `strcmp`. This is the single biggest spec departure; for our usage (binding fixed addresses from a config file) it's correct.

## What we do not support

- **OSC 1.1 timetags / scheduling**. Bundles parsed; timetag ignored.
- **OSC over TCP, SLIP-framed serial**. UDP only.
- **OSC multicast / broadcast**. Single unicast bind on INADDR_ANY.
- **Address pattern matching** (wildcards). Literal `strcmp` only.
- **Outgoing OSC** (sender). v1 is receive-only. See [Phase 3 in OSC_PLAN.md](../../OSC_PLAN.md) for the optional send-side that's intentionally deferred.
- **Multi-argument dispatch**. Only the first arg of each message is consumed.
- **OSC handshake / discovery / heartbeat**. None of that exists in the OSC 1.0 spec; we don't add it.

## Wire-level debugging

### Capture inbound packets

macOS `tcpdump`:

```bash
sudo tcpdump -i lo0 -X 'udp port 7700'
```

(`lo0` for localhost-only sources; `en0` for over-the-network sources.)

The `-X` flag dumps hex + ASCII so you can read the address string inline:

```
12:34:56.789012 IP localhost.54321 > localhost.7700: UDP, length 20
        0x0000:  4500 0030 0001 0000 4011 7cba 7f00 0001  E..0....@.|.....
        0x0010:  7f00 0001 d431 1e14 001c fe2b 2f63 6d61  .....1.....+/cma
        0x0020:  2f64 6563 6179 0000 2c66 0000 3f59 999a  /decay..,f..?Y..
```

The payload starts at offset `0x1C`: `2f 63 6d 61 2f 64 65 63 61 79 00 00 2c 66 00 00 3f 59 99 9a` — exactly the bytes shown above.

### Use the bundled `osc_send.py`

`development/osc_send.py` is a 90-line zero-dep Python sender. Useful for testing without TouchDesigner or hardware:

```bash
# Float
python3 development/osc_send.py /cma/decay 0.85

# Int (force with i: prefix)
python3 development/osc_send.py /cma/preset/select i:3

# Bool
python3 development/osc_send.py /cma/layer/noise true

# Different host/port
python3 development/osc_send.py --host 192.168.1.50 --port 9000 /cma/shot
```

Source: [development/osc_send.py](../../development/osc_send.py).

### Use the `--osc-learn` flag

The fastest way to verify a message is arriving and being parsed:

```bash
./feedback --osc-listen --osc-learn
```

Every parsed message prints to stdout in the format:

```
[osc-learn] /cma/decay f=0.8500
[osc-learn] /cma/layer/noise T
[osc-learn] /cma/preset/select i=3
[osc-learn] /cma/shot (no args)
```

If a message arrives but you see no learn line, the parser rejected it — usually an unknown type tag or a malformed address. Move to `tcpdump` to see the bytes.

### Common malformed inputs

| Symptom | Likely cause |
| --- | --- |
| `tcpdump` shows the packet, no `[osc-learn]` line | Type tag char we don't support (e.g. `h` for int64) — message bailed |
| `[osc-learn]` shows address but `f=0.0000` always | Sender's float endianness wrong — must be big-endian |
| Address truncated in learn output | Sender sent >127 chars; truncated to fit `FeedbackOscMsg::address` |
| `[osc-learn]` shows `(no args)` for messages you expected to carry a float | Type tag missing or doesn't start with `,` |
| First few packets dropped then steady | Queue cap hit during a burst; normal recovery |

### Send a bundle manually

`osc_send.py` doesn't build bundles. For bundle testing, use any OSC library or this Python one-liner:

```python
import struct, socket
addr_pad = lambda s: s.encode() + b'\0' + b'\0' * ((-len(s)-1) % 4)
def msg(addr, f):
    return addr_pad(addr) + addr_pad(',f') + struct.pack('>f', f)
m1 = msg('/cma/decay', 0.5)
m2 = msg('/cma/sat', 0.9)
bundle = b'#bundle\0' + struct.pack('>q', 1) + struct.pack('>i', len(m1)) + m1 + struct.pack('>i', len(m2)) + m2
socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(bundle, ('127.0.0.1', 7700))
```

Both `/cma/decay 0.5` and `/cma/sat 0.9` should appear in `--osc-learn` output.

## Spec compliance summary

| Spec feature | Status |
| --- | --- |
| OSC 1.0 messages | ✅ full |
| OSC 1.0 bundles | ✅ parsed and recursed; timetag ignored |
| Types `i f s b T F N I` | ✅ all parsed (b skipped, s/S logged & ignored as first arg) |
| Types `h t d S c r m` | ❌ unsupported, message bailed |
| Type tag with no leading `,` | ✅ tolerated; treated as no-arg |
| Empty arg list (`,\0\0\0`) | ✅ tolerated |
| Pattern matching addresses | ❌ literal `strcmp` only |
| OSC 1.1 (no type tag, immediate, …) | ❌ |
| Nested bundles | ✅ recursive parse |
| UDP transport | ✅ |
| TCP transport | ❌ |
| SLIP-framed serial | ❌ |
| Sender (outgoing) | ❌ (Phase 3, deferred) |
