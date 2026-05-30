# OSC Control — Implementation Plan

**Branch:** `osc-control` (local)
**Goal:** Drive every parameter from OSC, in real time, from TouchDesigner and from a Novation Launch Control (via an OSC bridge or directly via the existing MIDI path).
**Non-goal:** Replace the MIDI path. The MIDI dispatch stays. OSC is a sibling that lands events on the same `Input::handler_(ActionId, float)` callback.

---

## Architecture read

The codebase is already structured for this. `input.h` exposes:

- `enum ActionId` — every parameter and toggle (~200+ entries, e.g. `ACT_ZOOM_AXIS`, `ACT_LAYER_NOISE`, `ACT_SCREENSHOT_HIRES`)
- `struct ActionInfo { id, name, kind, group, desc }` + `action_info_by_name(const char*)`
- `enum BindSource { SRC_KEY, SRC_GAMEPAD_BTN, SRC_GAMEPAD_AXIS, SRC_MIDI_CC, SRC_MIDI_CC14, SRC_MIDI_NOTE }` ← **we add `SRC_OSC_F` and `SRC_OSC_TRIG`**
- `struct Binding { action, source, code, modmask, scale, invert, deadzone, absolute, relative, delta, bipolar, shifted, context }`
- `Input::pollMidi()` / `pollKeyboard()` — drain queues, dispatch through one callback
- A `bindings.ini` parser with `[keyboard]`, `[gamepad]`, `[midi]` sections — we add `[osc]`

The MIDI path proves the shape. OSC mirrors it.

---

## Phase 1 — OSC ingestion (the actual unlock)

**Deliverable:** `feedback --osc-listen` opens a UDP port (default 7700). Any OSC message whose address matches a `[osc]` binding in `bindings.ini` is dispatched as if it were a MIDI CC of the same magnitude.

### Files
- **new** `osc.h` / `osc.cpp` — minimal OSC server (UDP socket + parser, no deps)
- **edit** `input.h` — add `SRC_OSC_F` (float arg → axis) and `SRC_OSC_TRIG` (any arg → trigger/discrete)
- **edit** `input.cpp` — parse `[osc]` section; `Input::pollOsc()` that drains the OSC queue and dispatches the same way as `pollMidi`
- **edit** `main.cpp` — CLI flags `--osc-listen [port]` (default 7700) and `--osc-learn`; call `Input::pollOsc()` in the frame loop
- **edit** `Makefile.macos` — add `osc.o`, no new lib needed (just BSD sockets, already linked via the system)

### OSC parser scope (minimal, ~200 LOC)
We don't need oscpack/liblo. OSC 1.0 is a brutally simple wire format:
- 4-byte aligned address string (e.g. `/cma/zoom`)
- 4-byte aligned type tag string (e.g. `,f` or `,i` or `,ifs`)
- Args packed in order (32-bit big-endian for int/float)

We support:
- Address pattern matching (literal only — no `?`/`*`/`[]` wildcards in v1)
- Types: `f` (float), `i` (int → cast to float), `s` (ignored, log only), `T`/`F` (true/false → 1.0/0.0)
- Single-message UDP datagrams (no bundles in v1 — we'll log + skip bundles)

That's enough for TouchDesigner's OSC Out CHOP and any standard OSC source.

### `[osc]` bindings syntax
Mirroring the existing `[midi]` syntax:

```ini
[osc]
listen = 7700                          # port (overridden by --osc-listen)
learn  = on                            # print every incoming address+value
prefix = /cma                          # optional, prepended to all action.name
                                       # addresses below

# Continuous knobs/faders (axis-shaped: float arg, 0..1 expected, scaled)
zoom.axis            = osc:/cma/zoom                 [scale=1.0] [bipolar]
decay.axis           = osc:/cma/decay                [scale=1.0]
out_fade.axis        = osc:/cma/outfade              [bipolar]
shape_count.axis     = osc:/cma/shape/count

# Triggers / toggles (any arg with >0.5 = press)
layer.noise          = osc:/cma/layer/noise
layer.inject         = osc:/cma/layer/inject
screenshot           = osc:/cma/shot
screenshot_hires     = osc:/cma/shot/hires
preset.next          = osc:/cma/preset/next
preset.prev          = osc:/cma/preset/prev

# Pattern selects (DISCRETE, fires once when value > 0.5)
pattern.hbars        = osc:/cma/pattern/1
pattern.rings        = osc:/cma/pattern/7
```

Flags reused from MIDI: `scale=X`, `invert`, `deadzone=X`, `bipolar` (remap 0..1 → -1..+1), `delta` (dispatch change since last value).

### CLI
```
--osc-listen [PORT]    enable OSC ingestion (default port 7700)
--osc-learn            print every incoming OSC address+arg (mapping mode)
--list-actions         dump action name table (so you can author [osc] easily)
```

### Acceptance test
1. `./feedback --osc-listen --osc-learn` opens port 7700
2. From another terminal, `oscsend localhost 7700 /cma/decay f 0.85` → log shows `[osc-learn] /cma/decay f=0.850` AND decay parameter moves to 0.85
3. Same from TouchDesigner OSC Out CHOP — works
4. Hot-reload `bindings.ini` (already supported via `--reload`) picks up new OSC bindings without restart

---

## Phase 2 — Launch Control mapping

The Launch Control is **MIDI native** (USB MIDI class-compliant). Two ways to drive Crutchfield from it:

### Path A (simpler, no OSC) — direct MIDI
LC plugs into the Mac, the existing `[midi]` system sees it. We ship a starter `bindings.ini` snippet for the LC XL layout (8 sliders + 24 knobs + 16 pads). No code, just config.

```ini
[midi]
port = Launch Control XL
learn = off

# LC XL Factory Template — channel 1
# Top knob row CC 13..20
zoom.axis     = cc:13   ch=1   delta
theta.axis    = cc:14   ch=1   delta
chroma.axis   = cc:15   ch=1
blur_x.axis   = cc:16   ch=1
# Middle knob row CC 29..36
sat.axis      = cc:29   ch=1
hue.axis      = cc:30   ch=1
gamma.axis    = cc:31   ch=1
# Bottom knob row CC 49..56  → physics/thermal
sensor_gamma.axis = cc:49 ch=1
therm_amp.axis    = cc:50 ch=1
# Sliders CC 77..84
decay.axis    = cc:77   ch=1
external.axis = cc:78   ch=1
noise.axis    = cc:79   ch=1
couple.axis   = cc:80   ch=1
out_fade.axis = cc:81   ch=1   bipolar
# Pads (notes) - layer toggles
layer.warp     = note:41  ch=1
layer.optics   = note:42  ch=1
layer.noise    = note:43  ch=1
layer.inject   = note:44  ch=1
# bottom pad row → patterns/triggers
pattern.dot      = note:57 ch=1
pattern.checker  = note:58 ch=1
screenshot       = note:59 ch=1
preset.next      = note:60 ch=1
```

The actual CCs vary by template (LC XL has Factory 1–8 + User 1–8). We ship a default for Factory 1 and document the rest. **The user does the mapping in ~3 min by writing rows of `bindings.ini`.**

### Path B — LC routed through TouchDesigner as OSC
For users who want every controller to converge on a single OSC namespace:
- TD `MIDI In CHOP` (Launch Control) → `OSC Out CHOP` (localhost:7700)
- Crutchfield receives via Phase 1 OSC ingestion
- Benefits: TD can transform, blend, automate, sequence between LC and other sources before the value lands on Crutchfield
- This requires only Phase 1 done; no Crutchfield-side changes

We ship a `touchdesigner/launch_control_to_osc.toe` template under `development/` showing this routing.

---

## Phase 3 — OSC output (state echo) — optional

For bidirectional TD use (e.g. UI mirrors the current value on screen, or you sequence values across presets and want TD to follow):

- After dispatch, optionally emit `/cma/<action.name> f <value>` back out on a sender port (default 7701, configurable)
- Triggered on parameter change only (debounce per-tick to avoid flooding)
- Useful for: TD UI panels that reflect Crutchfield state, OSC sequencers that need echo for correctness, future motorized-fader controllers

**Scope:** ~150 LOC. Skip if not needed; Phase 1 is the unlock.

---

## Phase 4 — TouchDesigner template

Ship `development/touchdesigner/crutchfield_osc_control.toe` containing:
- OSC Out CHOP pre-configured for localhost:7700
- A panel of sliders + buttons that send to the standard `/cma/*` addresses
- Optional OSC In CHOP listening on 7701 (Phase 3) for echo

Also ship `development/touchdesigner/launch_control_to_osc.toe` from Phase 2 Path B.

---

## File inventory

```
osc.h                                     [NEW]   ~80 LOC   header for OSC server + parser
osc.cpp                                   [NEW]   ~250 LOC  UDP listener thread + OSC parse + lockfree queue
input.h                                   [EDIT]  +2 LOC    SRC_OSC_F, SRC_OSC_TRIG enum members
input.cpp                                 [EDIT]  +150 LOC  [osc] section parser, pollOsc(), osc: prefix in binding spec
main.cpp                                  [EDIT]  +30 LOC   CLI flags, init, call pollOsc() in loop
Makefile.macos                            [EDIT]  +2 LOC    osc.o in SRCS
Makefile                                  [EDIT]  +2 LOC    (Linux parity; Windows uses winsock — small ifdef)
OSC_REFERENCE.md                          [NEW]   ~200 LOC  address map, examples, learn-mode workflow
bindings.ini.examples/launch_control_xl.ini  [NEW]   ready-to-use LC XL map
bindings.ini.examples/touchdesigner.ini   [NEW]   starter for TD-driven OSC
development/touchdesigner/*.toe           [NEW]   2 template files
```

---

## Implementation order

1. **Cut branch + plan** ← we are here
2. `osc.h` / `osc.cpp` — UDP listener + parser, run in `--osc-learn` only mode (print received). Verify with `oscsend` from CLI.
3. `input.h` enum + `Binding` extensions. `input.cpp` `[osc]` parser + `pollOsc()` dispatch.
4. `main.cpp` wire `--osc-listen` + `--list-actions`. Verify decay knob moves from `oscsend`.
5. `bindings.ini` example for Launch Control XL (direct MIDI path) + verify on Sean's actual hardware.
6. TouchDesigner template `.toe` + `OSC_REFERENCE.md`.
7. (Optional) Phase 3 OSC echo + bidirectional TD template.

---

## Risks / open questions

- **OSC bundles** — we skip in v1 (TD doesn't emit bundles by default). If a tool needs them, +30 LOC.
- **Port collision** — 7700/7701 are standard but not reserved. Easy override via flag.
- **Threading** — OSC ingest runs on a background thread, push to lockfree queue, drain on main thread (mirrors the existing MIDI design exactly). No new sync primitives.
- **Windows build** — sockets need `winsock2.h` + `WSAStartup` + link `ws2_32`. Trivial ifdef. Apple/Linux use plain BSD sockets.
- **Launch Control variant** — XL has 8 sliders + 24 knobs + 16 pads. "Mini" has fewer. Specify which when we write the bindings example. **Sean: confirm which Launch Control you have.**

---

## What this enables

- **TouchDesigner**: arbitrary OSC Out CHOP into Crutchfield, every parameter live
- **Launch Control (direct)**: 30 minutes of `bindings.ini` and every knob is mapped
- **Launch Control (via TD)**: LC drives TD CHOPs → OSC out → Crutchfield, with all TD's blending/sequencing/automation in the middle
- **OSC sequencers** (Vezér, Ableton via OSC bridge, custom Max patches): drive Crutchfield as another instrument
- **Future**: any networked control surface, Lemur, TouchOSC, etc.
