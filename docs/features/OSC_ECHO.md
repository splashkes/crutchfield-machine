# OSC echo — every dispatched action emits outbound OSC

When `--osc-echo HOST:PORT` is set (or `[osc] echo = HOST:PORT` in `bindings.ini`), every action dispatched by ANY source — keyboard, gamepad, MIDI, OSC ingress, audio analyzer, Link beat — emits an outbound OSC message at `/cma/echo/<action.name>` with the current magnitude.

This is the single layer that unlocks bidirectional integration: TouchDesigner UI panels that mirror Crutchfield's state, sequencers that read back what they sent, multi-instance sync with one master driving N followers.

## Usage

```bash
# CLI: echo to localhost:7701
./feedback --osc-listen --osc-echo 127.0.0.1:7701

# Or in bindings.ini
[osc]
listen = 7700
echo   = 127.0.0.1:7701

# Or shorter forms
echo = :7701          # implies 127.0.0.1
echo = 7701           # same
```

## What gets emitted

For every `handler_(action, mag)` call:

```
/cma/echo/<action.name>   ,f   <mag>
```

Examples (all observed in a real session):

```
/cma/echo/dyn.decay.axis    ,f  0.7500
/cma/echo/color.sat.setAxis ,f  0.5000
/cma/echo/layer.noise       ,f  1.0000
/cma/echo/app.screenshot    ,f  1.0000
```

The address is built from the catalogue's `ActionInfo::name`, which means it's stable across MIDI bindings, OSC bindings, and direct keyboard presses. A press of the M key fires `app.math` and the echo emits `/cma/echo/app.math`.

## Receiver side

### Python (no-deps)

```python
import socket, struct
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('127.0.0.1', 7701))
while True:
    data, _ = s.recvfrom(2048)
    # OSC: address null-term + pad4, type tag, args
    end = data.index(b'\0')
    addr = data[:end].decode()
    tt_off = ((end + 4) // 4) * 4
    tt_end = data.index(b'\0', tt_off)
    tt = data[tt_off:tt_end].decode()
    pl = ((tt_end + 4) // 4) * 4
    if tt == ',f':
        val = struct.unpack('>f', data[pl:pl+4])[0]
        print(addr, val)
```

### TouchDesigner

OSC In CHOP:
- Network Port: `7701`
- Channels named after their `/cma/echo/*` addresses

Then drive a UI Panel Slider COMP from the corresponding echo channel. The slider visually follows whatever changes Crutchfield's parameter — whether it came from your TD output OR a hardware controller OR a keyboard press.

## Multi-instance sync

Run two Crutchfields on the same LAN. Configure the master to echo to the follower's listen port:

```bash
# On the master Mac
./feedback --osc-listen --osc-echo 192.168.1.50:7700

# On the follower Mac
./feedback --osc-listen
# Bind every /cma/echo/* address to the matching action — see follower-bindings.ini below
```

Follower's `bindings.ini`:

```ini
[osc]
listen = 7700

# Mirror everything the master does
dyn.decay.axis     = osc:/cma/echo/dyn.decay.axis
color.sat.setAxis  = osc:/cma/echo/color.sat.setAxis
layer.noise        = osc:/cma/echo/layer.noise
# ...one line per action you want mirrored
```

For a wholesale "mirror everything" follower, use the wildcard:

```ini
[osc]
# This single binding sets decay from any of: /cma/decay /cma/echo/dyn.decay.axis
# (because both addresses match the wildcard)
dyn.decay.axis = osc:/cma/*decay*
```

(See [OSC patterns](../osc/PROTOCOL.md#wildcards) for full wildcard syntax.)

## Performance

One UDP send per `handler_` call. At 60 fps with 4 active axes that's ~240 packets/sec — negligible. The send is non-blocking (`SO_BROADCAST`-style fire-and-forget UDP).

If you don't want echo on every dispatch (e.g. to filter to certain actions), set `--osc-echo` per session and use a downstream filter, or open an issue for an `[osc.echo.filter]` config block.

## What gets echoed

| Source of dispatch | Echoed? |
| --- | --- |
| Keyboard | yes |
| Gamepad | yes |
| MIDI CC / Note / CC14 | yes |
| OSC ingress | yes (creates a feedback loop if pointed at self — don't) |
| Audio analyzer | yes (60 Hz per audio binding — high rate) |
| Ableton Link | yes (Link beat events emit) |
| Macros | each step emits separately |
| State snapshots | the save/recall action emits, the per-parameter writes do too |

## What's NOT echoed

- Parameters that change inside `apply_action` without going through `handler_` (e.g. internal preset loads). The echo lives at the `handler_` wrapper, not below it.
- Engine-internal state (FBO contents, current shader, etc.). Echo is for the action layer only.

## Implementation pointers

- `feedback_osc_set_echo()` / `feedback_osc_send_f()` in `osc.cpp`
- `Input::setOscEcho()`, `Input::echoActionDispatch()` in `input.cpp`
- `g_input.setHandler([](id, mag) { apply_action(id, mag); g_input.echoActionDispatch(id, mag); })` wrapping in `main.cpp`

## Related

- [BINDINGS.md](../osc/BINDINGS.md) — binding syntax
- [PROTOCOL.md](../osc/PROTOCOL.md) — OSC wire format
- [COOKBOOK.md](../osc/COOKBOOK.md) — recipes including TD UI mirror
