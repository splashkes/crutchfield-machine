# Launch Control v1 — full guide

Everything you need to drive Crutchfield Machine from a Novation Launch Control (the original, 2013–, not the XL, not the Mini).

## Identifying which Launch Control you have

| Device | Year | Form factor | Faders | Knobs | Pads | Side buttons |
| --- | --- | --- | --- | --- | --- | --- |
| **Launch Control** (this guide) | 2013 (still sold) | small, ~25 × 12 cm | 0 | 8 (one row) | 16 (2 × 8) | 4 (Up/Down/L/R) |
| Launch Control XL | 2014 | wide, ~46 × 19 cm | 8 | 24 (3 × 8) | 16 (2 × 8) | column on right |
| Launch Control Mini | 2024 | tiny | 0 | 8 | 8 | 0 |

If yours looks like the first row (small device, 8 knobs across the top, 16 pads in 2 rows below, 4 small side buttons), this guide is for you. The XL is much wider with sliders; the Mini has fewer pads.

## What you can drive

With the ready-made mapping, every control on the LC v1 does something useful in Crutchfield:

| Control | Bound action | What it does |
| --- | --- | --- |
| Knob 1 (top-left) | `color.sat.setAxis` | saturation setpoint |
| Knob 2 | `color.hue.setAxis` | hue rate setpoint |
| Knob 3 | `color.gamma.setAxis` | gamma setpoint |
| Knob 4 | `color.contrast.setAxis` | contrast setpoint |
| Knob 5 | `dyn.decay.axis` | feedback decay (the main "memory" knob) |
| Knob 6 | `dyn.external.axis` | camera blend amount |
| Knob 7 | `optics.blurX.setAxis` | horizontal blur |
| Knob 8 | `optics.blurY.setAxis` | vertical blur |
| Top pad 1 | `layer.warp` | toggle warp layer |
| Top pad 2 | `layer.optics` | toggle optics layer |
| Top pad 3 | `layer.color` | toggle color layer |
| Top pad 4 | `layer.decay` | toggle decay layer |
| Top pad 5 | `layer.noise` | toggle noise layer |
| Top pad 6 | `layer.couple` | toggle couple (cross-field feedback) |
| Top pad 7 | `layer.external` | toggle camera input |
| Top pad 8 | `layer.inject` | toggle inject layer |
| Bottom pad 1 | `pattern.hbars` | select H-bars pattern |
| Bottom pad 2 | `pattern.vbars` | select V-bars pattern |
| Bottom pad 3 | `pattern.dot` | select dot pattern |
| Bottom pad 4 | `pattern.checker` | select checker pattern |
| Bottom pad 5 | `pattern.rings` | select rings pattern |
| Bottom pad 6 | `pattern.spiral` | select spiral pattern |
| Bottom pad 7 | `app.screenshot` | take a screenshot |
| Bottom pad 8 | `app.screenshot.hires` | take a 2× supersampled screenshot |
| Side Up button | `preset.prev` | previous preset |
| Side Down button | `preset.next` | next preset |
| Side Left button | `app.fullscreen` | toggle fullscreen |
| Side Right button | `app.clear` | clear all fields to black |

22 mappings. Every action name validated against the catalogue.

## Setup (60 seconds)

```bash
# 1. Append the ready-made bindings to your user file
cat ~/workspace/crutchfield-machine/bindings.examples/launch_control_v1.ini \
  >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"

# 2. Plug in the Launch Control via USB

# 3. Launch
cd ~/workspace/crutchfield-machine
./feedback
```

The Launch Control enumerates as a CoreMIDI device. Crutchfield finds it via substring match against `port = Launch Control` in the `[midi]` section. Knobs and pads work immediately.

## How Factory Template 1 works on the LC v1

The Launch Control has 8 Factory Templates and 8 User Templates. Tap the **Template** button (top-right of the device) to cycle through them.

Factory Template 1 (the default, green LED) sends on **MIDI channel 9** with these numbers:

```
Knobs 1..8:           CC 21, 22, 23, 24, 25, 26, 27, 28
Top pad row 1..8:     notes 9, 10, 11, 12, 25, 26, 27, 28
Bottom pad row 1..8:  notes 41, 42, 43, 44, 57, 58, 59, 60
Side Up / Down:       notes 114 / 115
Side Left / Right:    notes 116 / 117
```

Factory Templates 2..8 send on channels 10..16 with the same CC/note layout. Switching templates is a quick way to have multiple banks for different shows.

## Using a different template

If you switch to Factory Template 2, change `ch=9` to `ch=10` throughout the bindings:

```bash
sed -i.bak 's/ch=9/ch=10/g' \
  "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"
```

Restart Crutchfield. All bindings now respond to Factory Template 2.

## Using a User Template (the one you've used with plugins)

User Templates are user-configured. You probably already have one set up from other plugin work — each control sends a unique CC/note you defined.

**Workflow to map a User Template**:

1. Launch with MIDI learn enabled:

```bash
./feedback --midi-learn
```

2. Tap **Template** on the Launch Control and select your User Template.

3. Touch each control once. The console prints what it sends:

```
[midi-learn] ch=1 cc:42 val=64
[midi-learn] ch=1 note:5 vel=127 on
```

4. Note each knob's `ch` and `cc`, each pad's `ch` and `note`.

5. Copy the example file and substitute:

```bash
cp bindings.examples/launch_control_v1.ini /tmp/lc_user.ini
# edit /tmp/lc_user.ini — change cc/note/ch numbers to match what you observed
# then append:
cat /tmp/lc_user.ini >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"
```

User Templates use channels 1–8 (vs 9–16 for Factory).

## Pad colors

The LC v1 has 16 multi-color RGB pads. By default they light up when pressed but Crutchfield doesn't currently send MIDI back to the device to control colours (that would require LED feedback support on the LC v1 side). The pads will flash on press but no scene-aware lighting yet.

If you want this: it's possible. The MIDI dispatcher in `input.cpp` already calls `sendMidiNote` for the DDJ-FLX2's LED feedback. Adding LC v1 LED feedback is a small change in `main.cpp::sync_layer_leds`. File an issue if you want it.

## Combining LC v1 with TouchDesigner

You can have both active at the same time:

```bash
# Append both
cat bindings.examples/launch_control_v1.ini >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"
cat bindings.examples/crutchfield_touchdesigner.ini >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"

# Launch with OSC enabled
./feedback --osc-listen
```

Now LC v1 knobs/pads work directly via MIDI, AND TouchDesigner OSC traffic to `/cma/*` also dispatches. Both fight for the same actions — last write wins on continuous params, both toggle independently on discretes.

For a cleaner setup where TD owns everything: route LC v1 MIDI through TD as a bridge (see [docs/touchdesigner/GETTING_STARTED.md](../touchdesigner/GETTING_STARTED.md) for the network).

## Tips

### Map an "all layers on/off" macro to one pad

The LC v1 has 16 pads and only 8 layers fit one row. Use a TD macro:

- Map a pad to send 10 OSC messages in sequence: `/cma/layer/warp 1`, `/cma/layer/optics 1`, etc.
- In Crutchfield's bindings, each layer.X is bound to the corresponding `/cma/layer/X`. One pad press triggers all 10.

Or use a single existing Crutchfield action that does the bulk thing (`layer.toggleArmed` cycles through an armed-layer concept — see [docs/osc/ACTIONS.md](../osc/ACTIONS.md)).

### Use knob 5 (decay) sparingly

Decay is the most impactful knob. Small movements (0.85 ↔ 0.99) produce huge changes in feel. The LC v1's knobs are infinite-resolution within their 0..127 7-bit MIDI CC range, so the steps are coarse. For ultra-fine decay control, route through TD with a Lag CHOP first.

### Save your live performance state

```bash
# Recording mode
./feedback --osc-listen --log-usage
```

`--log-usage` writes a CSV of every dispatched action. Combined with `--osc-learn`, you have a full record of "Knob 5 went to 0.94 at 12:35:08.123" — useful for replaying a show.
