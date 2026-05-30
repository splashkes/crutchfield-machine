# Ableton Link — networked tempo sync

Crutchfield is now a Link peer. Multiple instances on the same LAN sync tempo and start/stop. The Link layer also bridges to Ableton Live, Resolume, VDMX, Bitwig, Reason, Numerology, Algoriddim djay — anything that speaks Link.

## What is Ableton Link

Link is Ableton's open-source network-tempo protocol. Apps and devices on the same network (or just localhost) form a session where every peer shares:

- A common tempo (BPM)
- A common beat clock (phase)
- An optional start/stop transport signal

You don't pick a master. Tempo changes propagate. New peers join in phase. Wired Ethernet sub-millisecond, Wi-Fi a few ms.

Reference: <https://www.ableton.com/en/link/>.

## Enabling

```bash
# Off by default. Enable on startup:
./feedback --link

# Or toggle at runtime via the link.toggle action:
[osc]
link.toggle = osc:/cma/link/toggle
```

When enabled, Crutchfield advertises itself as a Link peer named "Crutchfield Machine" and starts seeing other peers on the network. When disabled, no network traffic.

## Built-in actions

```
link.toggle      enable/disable network discovery
link.tap         local tap-tempo into Link
link.transport   start/stop sync (propagates to all peers)
```

Bind these to a keyboard key, MIDI button, or OSC trigger:

```ini
[osc]
link.toggle    = osc:/cma/link/on
link.tap       = osc:/cma/link/tap
link.transport = osc:/cma/link/play
```

## The `link:` binding source

```ini
[osc]                  ; section irrelevant; the keyPart prefix decides
warp.zoom.axis     = link:phase bipolar       ; sweep zoom over each beat
layer.noise        = link:beat                ; fires once per beat
shape.size.axis    = link:bpm scale=0.005     ; size follows network tempo
overlay.something  = link:peers scale=0.5     ; number of peers, ranged
```

Channels:

| Channel | Type | Range / behavior |
| --- | --- | --- |
| `phase` | continuous | 0..1 within each beat-quantum (default quantum = 4) |
| `beat`  | trigger    | fires once per beat (use with discrete/trigger actions) |
| `bpm`   | continuous | raw network tempo (e.g. 120.0) |
| `peers` | continuous | count of other Link peers on the session |

Flags work the same as OSC: `scale=X`, `invert`, `bipolar`.

## Setup recipes

### Sync with Ableton Live

1. In Ableton Live, click the **Link** indicator in the upper-left to enable.
2. `./feedback --link`
3. Both apps see each other in their Link displays.
4. Adjust tempo on either side — the other follows.

### Sync between two Crutchfields

```bash
# Mac A (master, drives tempo via tap)
./feedback --link

# Mac B (follower, picks up tempo + phase from peers)
./feedback --link
```

Both instances now share phase. Bindings to `link:phase` line up across both screens.

### Beat-locked preset cycle

```ini
[osc]
preset.next = link:beat scale=4    ; advance every 4 beats — wait no, this fires every beat
```

For "every 4 beats", we need a counter. Use a macro fired by Link beat that does its own counting:

```ini
[macros]
beat.4count.advance = preset.next   ; placeholder; real counter would need state

[osc]
macro.beat.4count.advance = link:beat
```

(In v1, Link beat fires every beat. Multi-beat triggers need an external sequencer. A future enhancement might add a `link:beat/N` syntax for "every Nth beat".)

### Drum-machine-locked feedback

```ini
[osc]
dyn.decay.axis    = link:phase bipolar           ; phase sweeps decay 0..1 each beat
warp.zoom.axis    = link:phase scale=0.05        ; subtle zoom pulse on each beat
```

Now whatever's driving Link tempo (drum machine, Ableton, hardware controller) directly modulates the feedback.

## How it works under the hood

`link_glue.cpp` wraps Ableton's `Link` class in `extern "C"` functions:

```c
void   link_init(double initial_bpm);
void   link_set_enabled(int on);
double link_tempo(void);
double link_beat_phase(double quantum);
int    link_did_beat(double quantum);   // 1 once per beat
int    link_num_peers(void);
int    link_is_playing(void);
void   link_set_playing(int on);
```

`Input::pollLink(dt)` runs each frame, reads `link_beat_phase(4.0)`, `link_tempo()`, `link_num_peers()`, `link_did_beat(4.0)`, then dispatches matching `SRC_LINK` bindings.

Beat detection on a polling loop is via the `did_beat` edge detector — it remembers the last beat value and returns 1 when the integer beat crosses, 0 otherwise. Frame-precise rather than sample-precise; at 60 fps that's ±16 ms jitter from the actual beat, fine for visuals.

## Network details

Link uses UDP multicast for discovery and small unicast packets for tempo updates. Firewall rules generally need to allow the multicast group `239.0.0.2` UDP port `20808` (Link's discovery channel) plus inbound UDP from peer IPs. macOS auto-approves; Windows may prompt; Linux uses systemd-networkd / firewalld config.

For airgapped use (just localhost), Link auto-discovers loopback peers — no internet needed.

## Cross-platform notes

| Platform | Status |
| --- | --- |
| macOS Apple Silicon | verified working, tested on this branch |
| macOS Intel | should work — same Link build path |
| Linux | flag set is wired in `linux/Makefile`; ASIO standalone vendored |
| Windows MSYS2/MinGW | wired in root `Makefile` |
| Windows MSVC | wired in `build_msvc.bat` |

`link_glue.cpp` uses `__has_include(<ableton/Link.hpp>)` — if the Link headers aren't reachable at compile time, every function compiles as a no-op stub. So fresh clones without `git submodule update --init` still build; Link just silently does nothing until you init the submodule.

## Limitations in v1

- No tap-tempo regression (single tap calls `link_set_tempo` with the current tempo, effectively a no-op for setting; you need an external tap generator). A multi-tap tempo estimator is a small future enhancement.
- Beat triggers fire every beat. No `link:beat/4` or `link:bar` shortcuts yet — use a macro counter, or wire a TouchDesigner Trigger CHOP downstream.
- Quantum is fixed at 4. (Quantum = beats per bar from Link's perspective.) Override would need a new binding spec like `link:phase/8`.

## Implementation pointers

- `link_glue.h` / `link_glue.cpp` — extern "C" wrapper with stub fallback
- `input.h`: `SRC_LINK`, `ACT_LINK_TOGGLE/TAP/TRANSPORT`, `pollLink()`
- `input.cpp`: `link:` prefix parser, `pollLink()` dispatch loop
- `main.cpp`: `link_init(120.0)` at startup, `--link` CLI flag, `apply_action` handles the three `link.*` actions
- `Makefile.macos` / `linux/Makefile` / `Makefile` (mingw) / `build_msvc.bat` — Link compile flags per platform
- `vendor/link` — cloned with `git clone --depth 1 https://github.com/Ableton/link.git vendor/link && cd vendor/link && git submodule update --init --recursive`
