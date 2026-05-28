# OSC Cookbook

Ready-to-use recipes for common control-surface and OSC-source scenarios. Each recipe assumes you've already built Crutchfield and the OSC listener works (see [docs/README.md](../README.md) for the 30-second setup).

---

## 1. Audio-reactive decay from TouchDesigner

**Goal**: Crutchfield's feedback decay follows the loudness envelope of the music. Loud passages = short decay (jittery, alive). Quiet passages = long decay (lingering, dreamy).

**TD network**:

```
[ Audio Device In CHOP ]   ← system audio input
        │
        ▼
[ Analyze CHOP ]            ← Function: RMS Power, Window: 0.2s
        │
        ▼
[ Lag CHOP ]                ← Lag 1: 0.05, Lag 2: 0.15 (smoothing tail)
        │
        ▼
[ Math CHOP ]               ← Combine: Multiply, Add: 0.3, Multiply: 0.6
        │                     (remap 0..1 audio to 0.3..0.9 decay range)
        ▼
[ Rename CHOP ]             ← Channel: cma/decay
        │
        ▼
[ OSC Out CHOP ]            ← 127.0.0.1:7700, Send Every Frame: on
```

**Crutchfield bindings.ini**:

```ini
[osc]
listen = 7700
dyn.decay.axis = osc:/cma/decay
```

The Math CHOP's add/multiply keeps the decay in a usable range (raw audio RMS 0..1 maps to decay 0.3..0.9 — never quite 0, never quite 1).

---

## 2. Beat-locked preset cycling

**Goal**: Every downbeat advances to the next preset.

**TD network**:

```
[ Beat CHOP ]               ← Tempo: 120 (or driven by audio analyzer)
        │
        ▼
[ Filter CHOP ]             ← keep only Beat 1 (downbeat)
        │
        ▼
[ Trigger CHOP ]            ← Threshold Up: 0.5, single-shot
        │
        ▼
[ Rename CHOP ]             ← Channel: cma/preset/next
        │
        ▼
[ OSC Out CHOP ]
```

**Crutchfield bindings.ini**:

```ini
[osc]
listen = 7700
preset.next = osct:/cma/preset/next
```

The `osct:` (trigger forced) ensures every Trigger CHOP pulse fires the preset advance — even if the action is technically discrete and would auto-bind as `osc:`.

---

## 3. Multi-source blend (LC + audio + sequencer)

**Goal**: A Launch Control slider provides the "scene intensity" baseline. An audio envelope modulates it. A TD timeline sequencer provides automated drops.

**TD network**:

```
[ MIDI In CHOP ]              [ Audio In CHOP → Analyze ]    [ Timeline CHOP ]
   (LC fader → 0..1)              (RMS → 0..1)                  (lane: 0..1)
        │                              │                            │
        ▼                              ▼                            ▼
        └──────────┐    ┌──────────────┘                            │
                   ▼    ▼                                           ▼
              [ Math CHOP ]                                  [ Math CHOP ]
              combine: Multiply (mix)                        combine: Add
                   │                                                │
                   └─────────────────┬──────────────────────────────┘
                                     ▼
                              [ Limit CHOP ]   ← clamp 0..1
                                     │
                                     ▼
                              [ Rename CHOP ]  ← cma/decay
                                     │
                                     ▼
                              [ OSC Out CHOP ]
```

**Crutchfield bindings.ini**: same as recipe 1.

Now: LC fader sets the "floor" decay; audio multiplies it (loud sections punch through); timeline can layer additive drops on top.

---

## 4. Launch Control v1 direct MIDI (no TD needed)

**Goal**: Plug LC v1 in and have every knob/pad/button do something useful.

```bash
# 1. Append the ready-made bindings to your user file
cat bindings.examples/launch_control_v1.ini \
  >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"

# 2. Plug the controller in
# 3. Launch
./feedback
```

No OSC, no TD — just MIDI. Full map at [docs/launch-control/V1_GUIDE.md](../launch-control/V1_GUIDE.md).

---

## 5. Launch Control through TD as an OSC bridge

**Goal**: Same LC v1 hardware, but with TD's automation/blending on top.

**TD network**:

```
[ MIDI In CHOP ]               ← Device: Launch Control
   (channels: c9c21..c9c28 = knobs,
              c9n9..c9n12, c9n25..c9n28 = top pads,
              c9n41..c9n44, c9n57..c9n60 = bottom pads,
              c9n114..c9n117 = side buttons)
        │
        ▼
[ Lag CHOP ] (optional)        ← smooth knob jitter (Lag 1: 0.02)
        │
        ▼
[ Rename CHOP ]                ← see table below
        │
        ▼
[ OSC Out CHOP ]
```

**Rename mapping (sample)**:

| MIDI channel | TD channel name → OSC address |
| --- | --- |
| `c9c21` → `cma/sat`                |
| `c9c22` → `cma/hue`                |
| `c9c25` → `cma/decay`              |
| `c9n9` → `cma/layer/warp`          |
| `c9n41` → `cma/pattern/hbars`      |
| `c9n114` → `cma/preset/prev`       |

**Crutchfield bindings.ini**:

```bash
cat bindings.examples/crutchfield_touchdesigner.ini \
  >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"
```

Now you have all of TD's processing in the middle — slew limiters, envelopes, hold-to-latch buttons, scene recall, MIDI-clock-synced LFOs.

---

## 6. Headless rendering driven by external sequencer

**Goal**: A Python or Max/MSP script drives Crutchfield for an unattended install.

```python
# osc_seq.py - simple state-driving sequencer
import socket, struct, time

def osc_string(s):
    raw = s.encode() + b'\0'
    return raw + b'\0' * ((-len(raw)) % 4)

def msg(addr, f):
    return osc_string(addr) + osc_string(',f') + struct.pack('>f', f)

def trig(addr):
    return osc_string(addr) + osc_string(',T')

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
target = ('127.0.0.1', 7700)

t = 0
while True:
    # Slow sweep of decay
    import math
    decay = 0.6 + 0.3 * math.sin(t * 0.5)
    sock.sendto(msg('/cma/decay', decay), target)

    # Pattern change every 8 seconds
    if int(t) % 8 == 0 and (t - int(t)) < 0.05:
        sock.sendto(trig('/cma/preset/next'), target)

    time.sleep(0.05)
    t += 0.05
```

**Crutchfield bindings.ini**:

```ini
[osc]
listen = 7700
dyn.decay.axis = osc:/cma/decay
preset.next    = osct:/cma/preset/next
```

Run Crutchfield once (in fullscreen on the display), then `python3 osc_seq.py` in another shell. Decay breathes, presets rotate, no operator needed.

---

## 7. Mapping a controller you don't have docs for

**Goal**: You bought a used controller, don't know its CC layout.

```bash
# 1. Launch with learn enabled
./feedback --osc-listen --osc-learn

# (If using MIDI controller directly via [midi] section, use --midi-learn instead.)

# 2. Touch each control once. The console prints what it sends:
#    [midi-learn] ch=9 cc:21 val=42
#    [midi-learn] ch=9 note:9 vel=127 on
#    ...
#    or (for OSC sources):
#    [osc-learn] /unknown/path f=0.4200

# 3. Add lines to bindings.ini matching what you observed
```

This is the recommended workflow for User Templates on any Launch Control variant (where the CC/note numbers are user-configured).

---

## 8. Bi-directional OSC (TD UI mirrors Crutchfield state)

**Status**: NOT IMPLEMENTED in v1. Phase 3 of the OSC plan adds outgoing OSC for state echo. See `OSC_PLAN.md` § Phase 3.

If you need this today, workaround: drive Crutchfield from TD only (so TD already owns the state), and skip the round-trip.

---

## 9. OSC dropout / packet loss handling

**Goal**: Network is iffy (Wi-Fi from a tablet running TouchOSC), need graceful behaviour.

OSC over UDP is fire-and-forget; lost packets just don't arrive. For continuous controls (axis bindings) the next packet snaps to the new value. For triggers (screenshot, preset cycle) a lost packet means the action just doesn't fire.

**Mitigations**:

- Send axis values at 60 Hz (TD `OSC Out CHOP` default). One drop = one frame of staleness, imperceptible.
- For triggers, send the trigger twice (e.g. `osc_send 1.0; sleep 0.05; osc_send 0.0; sleep 0.1; osc_send 1.0`). Duplicate fires won't cause issues since `DISCRETE` actions only fire on the rising-edge value > 0.5.
- For mission-critical control (live show), run over wired Ethernet. UDP over Wi-Fi at high packet rates loses 1-5% during congestion.

---

## 10. Recording / replaying OSC sessions

**Goal**: Capture an OSC stream for later playback (rehearse a show, debug a glitch).

**Capture** (works for any OSC source):

```bash
# tcpdump → pcap, replay-friendly
sudo tcpdump -i lo0 -w session.pcap 'udp port 7700'
```

**Replay**:

```bash
sudo tcpreplay -i lo0 session.pcap
```

Or use any OSC-aware tool (Vezér, OssiaScore, custom Python) that records and plays back OSC dumps.

For per-message logging without pcap overhead, use `--osc-learn` and pipe to a file:

```bash
./feedback --osc-listen --osc-learn 2>&1 | tee osc_session.log
```

Each line is `[osc-learn] <addr> <type>=<value>`. Easy to parse, replay with a small Python script.

---

## 11. Multi-machine performance (separate render + control)

**Goal**: Crutchfield runs on a Mac mini hidden in a rack. A separate laptop running TouchDesigner controls it over local Ethernet.

```bash
# On the render Mac:
./feedback --osc-listen 7700

# On the control laptop, point TD's OSC Out CHOP at the render Mac:
# Network Address: 192.168.1.50 (render Mac's IP)
# Network Port:    7700
```

The listener binds INADDR_ANY (all interfaces) so it accepts inbound from any source on the LAN. No firewall config needed within a local subnet; if there's a firewall between, open UDP 7700.

For multiple controllers (one operator at a hardware desk + another on a tablet), all OSC sources just send to the same port. The listener doesn't track senders.

---

## 12. Logging every action fire (audit trail)

**Goal**: Know exactly what every parameter touched, when, and from where.

```bash
./feedback --osc-listen --log-usage
```

`--log-usage` opens a session-timestamped CSV in the user dir and writes one row per `handler_` fire. Combined with `--osc-learn`, you get OSC-arriving log lines AND a structured CSV of every dispatched action. Useful for post-show analysis or debugging "what changed at minute 47?"

CSV columns include source (KEY / MIDI_NOTE / MIDI_CC / GAMEPAD_BTN / GAMEPAD_AXIS / **OSC**), action ID, magnitude, and timestamp.

---

More recipes? File an issue or extend this doc. The OSC layer is intentionally thin so any new pattern is just "address + binding + downstream tool."
