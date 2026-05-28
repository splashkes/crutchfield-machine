# OSC Reference

Real-time OSC control for every parameter in the action catalogue. Built for TouchDesigner, Novation Launch Control (via TD as bridge or directly via MIDI), and any other OSC-speaking control surface.

The OSC layer dispatches through the same `Input::handler_(ActionId, float)` callback the MIDI path uses. Anything you can bind to a key, gamepad, or MIDI message can be bound to an OSC address.

> **Looking for the deep documentation?** Start at [`docs/README.md`](docs/README.md).
> Topic-specific docs:
> - [`docs/osc/ARCHITECTURE.md`](docs/osc/ARCHITECTURE.md) — how the OSC layer fits into the dispatch model
> - [`docs/osc/PROTOCOL.md`](docs/osc/PROTOCOL.md) — OSC 1.0 wire format + our parser specifics
> - [`docs/osc/BINDINGS.md`](docs/osc/BINDINGS.md) — full bindings syntax + every flag
> - [`docs/osc/CLI.md`](docs/osc/CLI.md) — every command-line flag and config key
> - [`docs/osc/COOKBOOK.md`](docs/osc/COOKBOOK.md) — recipes for common scenarios
> - [`docs/osc/ACTIONS.md`](docs/osc/ACTIONS.md) — full catalogue (181 actions, grouped)
> - [`docs/osc/TROUBLESHOOTING.md`](docs/osc/TROUBLESHOOTING.md) — diagnostics
> - [`docs/launch-control/V1_GUIDE.md`](docs/launch-control/V1_GUIDE.md) — Launch Control original (2013, 2017)
> - [`docs/launch-control/XL_GUIDE.md`](docs/launch-control/XL_GUIDE.md) — Launch Control XL
> - [`docs/touchdesigner/GETTING_STARTED.md`](docs/touchdesigner/GETTING_STARTED.md) — TouchDesigner walkthrough

The summary below covers the basics. For deep dives, follow the links above.

---

## Quick start

```bash
# 1. Launch with OSC ingestion + learn mode
./feedback --osc-listen --osc-learn

# 2. From another terminal, send an OSC message
python3 development/osc_send.py /cma/decay 0.85

# 3. You'll see this in the feedback console:
#    [osc-learn] /cma/decay f=0.8500
#
# 4. Now wire a binding by adding to bindings.ini ([osc] section):
#    dyn.decay.axis = osc:/cma/decay
#
# 5. Restart. Sending /cma/decay 0.5 now moves the decay parameter live.
```

To dump every action name:

```bash
./feedback --list-actions
```

181 entries. Full catalogue at [`docs/osc/ACTIONS.md`](docs/osc/ACTIONS.md).

---

## CLI flags

| Flag | Effect |
| --- | --- |
| `--osc-listen [PORT]` | Open UDP listener (default port `7700`) |
| `--osc-learn` | Print every incoming OSC message to stdout (mapping mode) |
| `--list-actions` | Dump action.name + group + description, then exit |

CLI overrides `bindings.ini` `[osc]` config. Full reference: [`docs/osc/CLI.md`](docs/osc/CLI.md).

---

## `bindings.ini` `[osc]` section

```ini
[osc]
# Top-level keys
listen = 7700              # UDP port. 0 = disabled.
learn  = on                # print every incoming address+arg

# Continuous (axis) bindings — float arg expected in 0..1
dyn.decay.axis      = osc:/cma/decay
color.sat.setAxis   = osc:/cma/sat                 scale=1.0
warp.zoom.axis      = osc:/cma/zoom                bipolar
optics.blurX.setAxis= osc:/cma/blur/x              invert

# Trigger / toggle bindings — any arg >0.5 = press
layer.noise         = osc:/cma/layer/noise
app.screenshot      = osc:/cma/shot
preset.next         = osc:/cma/preset/next

# Force trigger semantics on a continuous-shaped address
# (useful for TD Pulse Trigger CHOPs that send 1.0 then 0.0)
app.screenshot      = osct:/cma/shot/now
```

### Binding spec

```
action.name = osc:/path/to/addr  [flags]
action.name = osct:/path/to/addr [flags]
```

- `osc:` chooses the source automatically from the action's kind:
  AK_STEP / AK_RATE → `SRC_OSC_F` (axis), AK_DISCRETE / AK_TRIGGER →
  `SRC_OSC_TRIG` (button).
- `osct:` forces `SRC_OSC_TRIG` regardless of action kind.

### Flags

| Flag         | Effect                                                  |
| ------------ | ------------------------------------------------------- |
| `scale=X`    | Multiply arg by X before dispatch                       |
| `invert`     | Negate arg before dispatch                              |
| `bipolar`    | Remap 0..1 → -1..+1 (right for centered controls)       |
| `delta`      | Dispatch *change* since last value (for relative knobs) |

Full semantics + worked examples in [`docs/osc/BINDINGS.md`](docs/osc/BINDINGS.md).

---

## Supported OSC types

| Tag | Meaning            | Mapped to                       |
| --- | ------------------ | ------------------------------- |
| `f` | float32 (BE)       | `arg_f` directly                |
| `i` | int32 (BE)         | cast to float                   |
| `T` | true               | `1.0`                           |
| `F` | false              | `0.0`                           |
| `N` | nil                | `0.0`                           |
| `I` | infinitum          | `0.0`                           |
| `s` | string             | parsed, ignored (logs in learn) |
| `b` | blob               | skipped                         |

Bundles (`#bundle`) are parsed recursively; each contained message is dispatched. Only the **first argument** of each message is consumed. Wire-level reference: [`docs/osc/PROTOCOL.md`](docs/osc/PROTOCOL.md).

---

## TouchDesigner integration

Walkthrough: [`docs/touchdesigner/GETTING_STARTED.md`](docs/touchdesigner/GETTING_STARTED.md).

Quick version:

Add an `OSC Out CHOP`:

| Parameter        | Value         |
| ---------------- | ------------- |
| Network Address  | `127.0.0.1`   |
| Network Port     | `7700`        |
| Send Mode        | `Send Channel Values` |
| Send Every Frame | `On`          |

Set the CHOP's channel names to match your `[osc]` binding addresses (e.g. a channel named `cma/decay` will send to `/cma/decay`). Any CHOP upstream — sliders, automation, LFOs, MIDI In, audio analysis — now drives Crutchfield.

Canonical address map: [`development/touchdesigner/crutchfield_osc_map.json`](development/touchdesigner/crutchfield_osc_map.json).
Matching bindings.ini: [`bindings.examples/crutchfield_touchdesigner.ini`](bindings.examples/crutchfield_touchdesigner.ini).

### Launch Control via TouchDesigner

If you want LC running through TD instead of direct MIDI:

```
MIDI In CHOP (Launch Control)
  → optional Math/Filter CHOPs (scale, smooth, blend with other sources)
  → OSC Out CHOP (127.0.0.1:7700)
```

Then bind Crutchfield to the OSC addresses TD emits. Recipe: [`development/touchdesigner/launch_control_xl_bridge.md`](development/touchdesigner/launch_control_xl_bridge.md).

---

## Launch Control — direct MIDI

Two ready-to-use bindings files for the two most common Launch Control variants:

| Device | File | Deep guide |
| --- | --- | --- |
| Launch Control (original / v1, 2013–) | [`bindings.examples/launch_control_v1.ini`](bindings.examples/launch_control_v1.ini) | [`docs/launch-control/V1_GUIDE.md`](docs/launch-control/V1_GUIDE.md) |
| Launch Control XL | [`bindings.examples/launch_control_xl.ini`](bindings.examples/launch_control_xl.ini) | [`docs/launch-control/XL_GUIDE.md`](docs/launch-control/XL_GUIDE.md) |

Append the contents of the one matching your hardware to your `bindings.ini` at `~/Library/Application Support/Crutchfield Machine/bindings.ini` and the controller is live on launch.

### LC v1 (original) — Factory Template 1

- 8 knobs → color/tone/optics setpoints (CC 21–28, ch 9)
- Top pad row → 8 layer toggles (notes 9–12, 25–28, ch 9)
- Bottom pad row → 6 pattern selectors + 2 screenshot triggers (notes 41–44, 57–60, ch 9)
- Side buttons → preset prev/next, fullscreen, clear (notes 114–117, ch 9)

### LC XL — Factory Template 1

- 8 sliders → decay, external, noise, couple setpoint, outfade (bipolar), fx wet, shape count, shape size
- Top knob row → color/tone setpoints + zoom/theta as deltas
- Middle knob row → optics + physics extras + translate setpoints
- Bottom knob row → thermal + sphere deltas
- Top pad row → 8 layer toggles
- Bottom pad row → 8 pattern selectors
- Right-side column → fullscreen, preset prev/next, screenshot

### User Templates (any variant)

If you already have a User Template configured from other plugin work, the CC/note numbers will be different and the device sends on channels 1–8 instead of 9–16. Fastest path to a working map:

```bash
./feedback --midi-learn   # touch each control, note the printed cc/ch
```

Then copy the relevant example file and substitute your numbers.

---

## Wire format reference

OSC 1.0 over UDP. Every datagram is one of:

- A single message: address string + type tag string + args
- A bundle: `#bundle\0` + 8-byte timetag + N × (4-byte size + element)

Address and type strings are null-terminated and padded to a multiple of 4 bytes. `i`/`f` args are 4 bytes big-endian. `s` is a null-terminated string padded to 4 bytes. `T`/`F`/`N`/`I` have no payload.

The parser in `osc.cpp` is a literal implementation of OSC 1.0 and should accept anything any standard OSC library produces. Full wire-level docs: [`docs/osc/PROTOCOL.md`](docs/osc/PROTOCOL.md).

---

## Troubleshooting

**`[osc] bind to port 7700 failed`** — another process owns the port. `lsof -nP -iUDP:7700` to find it.

**`[bindings] unknown action 'x' — skipped`** — typo or wrong action name. Run `./feedback --list-actions | grep <substring>`.

**`learn` shows messages but the parameter doesn't move** — check that the action name in your binding matches a real action AND that the action's kind is compatible.

Full diagnostic playbook: [`docs/osc/TROUBLESHOOTING.md`](docs/osc/TROUBLESHOOTING.md).
