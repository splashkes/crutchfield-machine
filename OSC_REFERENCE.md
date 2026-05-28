# OSC Reference — Crutchfield Machine

Real-time OSC control for every parameter in the action catalogue. Built
for TouchDesigner, Novation Launch Control (via TD as bridge or directly
via MIDI), and any other OSC-speaking control surface.

The OSC layer dispatches through the same `Input::handler_(ActionId,
float)` callback the MIDI path uses. Anything you can bind to a key,
gamepad, or MIDI message can be bound to an OSC address.

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

---

## CLI flags

| Flag                    | Effect                                                     |
| ----------------------- | ---------------------------------------------------------- |
| `--osc-listen [PORT]`   | Open UDP listener (default port `7700`)                    |
| `--osc-learn`           | Print every incoming OSC message to stdout (mapping mode)  |
| `--list-actions`        | Dump action.name + group + description, then exit          |

CLI overrides `bindings.ini` `[osc]` config.

---

## bindings.ini `[osc]` section

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

Bundles (`#bundle`) are parsed recursively; each contained message is
dispatched.

Only the **first argument** of each message is consumed. Multi-arg
messages have their later args parsed (for offset correctness) but
discarded.

---

## TouchDesigner integration

### TD → Crutchfield (parameter control)

Add an `OSC Out CHOP`:

| Parameter        | Value         |
| ---------------- | ------------- |
| Network Address  | `127.0.0.1`   |
| Network Port     | `7700`        |
| Send Mode        | `Send Channel Values` |

Set the CHOP's channel names to match your `[osc]` binding addresses
(e.g. a channel named `cma/decay` will send to `/cma/decay`). Any CHOP
upstream — sliders, automation, LFOs, MIDI In, audio analysis — now
drives Crutchfield.

A template `.toe` is at `development/touchdesigner/` (placeholder for
v1; full file added once the workflow is validated on your machine).

### Launch Control via TouchDesigner

If you want LC running through TD instead of direct MIDI:

```
MIDI In CHOP (Launch Control XL)
  → optional Math/Filter CHOPs (scale, smooth, blend with other sources)
  → OSC Out CHOP (127.0.0.1:7700)
```

Then bind Crutchfield to the OSC addresses TD emits. This gives you all
of TD's sequencing, automation, and blending between the LC and other
sources before the value reaches the feedback engine.

---

## Launch Control XL — direct MIDI

If you don't need TD in the middle, the LC XL just plugs into the Mac
and the existing MIDI path picks it up. Ready-to-use bindings live at:

```
bindings.examples/launch_control_xl.ini
```

Append the contents to your `bindings.ini` at
`~/Library/Application Support/Crutchfield Machine/bindings.ini` and the
controller is live on launch.

The example covers Factory Template 1 (the green-LED default):
- 8 sliders → decay, external, noise, couple setpoint, outfade
  (bipolar), fx wet, shape count, shape size
- Top knob row → color/tone setpoints + zoom/theta as deltas
- Middle knob row → optics + physics extras + translate setpoints
- Bottom knob row → thermal + sphere deltas
- Top pad row → 8 layer toggles
- Bottom pad row → 8 pattern selectors
- Right-side column → fullscreen, preset prev/next, screenshot

---

## Wire format reference

OSC 1.0 over UDP. Every datagram is one of:

- A single message: address string + type tag string + args
- A bundle: `#bundle\0` + 8-byte timetag + N × (4-byte size + element)

Address and type strings are null-terminated and padded to a multiple
of 4 bytes. `i`/`f` args are 4 bytes big-endian. `s` is a null-terminated
string padded to 4 bytes. `T`/`F`/`N`/`I` have no payload.

The parser in `osc.cpp` is a literal implementation of OSC 1.0 and
should accept anything any standard OSC library produces.

---

## Common patterns

### Smooth sweep from TD
TD's LFO CHOP → OSC Out CHOP. Smooth analog feel without delta-mode
gymnastics.

### Beat-locked triggers
TD's Beat CHOP → trigger out → OSC Out CHOP at `/cma/preset/next` on
each downbeat. Use `osct:` if you want momentary semantics.

### Multi-source blend
Two MIDI controllers + an audio envelope follower → Math CHOP blend in
TD → single OSC Out. Crutchfield sees one stream; TD owns the mix.

### Live mapping
Run `./feedback --osc-listen --osc-learn`, twiddle a TD slider, watch
the address+value print. Copy that address into `bindings.ini` and
restart. No code edits, no rebuilds.

---

## Troubleshooting

**`[osc] bind to port 7700 failed`** — another process owns the port.
`lsof -nP -iUDP:7700` to find it. Often it's a previous feedback that
didn't shut down cleanly.

**`[bindings] unknown action 'x' — skipped`** — typo or wrong action
name. Run `./feedback --list-actions | grep <substring>` to find the
right one. Action names are stable across versions.

**`learn` shows messages but the parameter doesn't move** — check that
the action name in your binding matches a real action (above) AND that
the action's kind is compatible. STEP/RATE actions want continuous
floats; DISCRETE/TRIGGER actions fire on values > 0.5.

**Bindings load but learn is empty when sending** — confirm the port
in your sender matches the port the feedback bound. The startup log
prints `[osc] listening on UDP port N`.

**Log output is delayed** — stdout is block-buffered when redirected.
Use `stdbuf -oL ./feedback ...` or pipe to `cat` for line buffering
during debugging.
