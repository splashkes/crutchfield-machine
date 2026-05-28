# Launch Control XL — full guide

Drive Crutchfield from a Novation Launch Control XL (the wide variant: 8 sliders + 24 knobs + 16 pads + right-side column).

If you have the original Launch Control (no sliders, only 8 knobs + 16 pads), see [V1_GUIDE.md](V1_GUIDE.md) instead.

## What you can drive

Full mapping at [bindings.examples/launch_control_xl.ini](../../bindings.examples/launch_control_xl.ini). 56 mappings covering every primary control on the device.

### Sliders (the 8 vertical faders) — performance controls

| Slider | Bound action | Notes |
| --- | --- | --- |
| 1 | `dyn.decay.axis` | the main "memory" knob |
| 2 | `dyn.external.axis` | camera blend amount |
| 3 | `dyn.noise.axis` | noise level |
| 4 | `dyn.couple.setAxis` | cross-field coupling |
| 5 | `outfade.axis` (`bipolar`) | output fade: 0=black, 1=neutral, 2=white |
| 6 | `fx.wet.axis` | VFX wet mix |
| 7 | `shape.count.axis` | inject shape count |
| 8 | `shape.size.axis` | inject shape size |

### Top knob row (Send A) — color / tone setpoints

| Knob | Bound action |
| --- | --- |
| A1 | `color.sat.setAxis` |
| A2 | `color.hue.setAxis` |
| A3 | `color.gamma.setAxis` |
| A4 | `color.contrast.setAxis` |
| A5 | `warp.zoom.axis` (`delta`) |
| A6 | `warp.theta.axis` (`delta`) |
| A7 | `optics.blurX.setAxis` |
| A8 | `optics.blurY.setAxis` |

### Middle knob row (Send B) — optics + inject

| Knob | Bound action | Note |
| --- | --- | --- |
| B1 | `optics.chroma+` (`delta`) | knob nudges chroma |
| B2 | `shape.rot.axis` | inject shape rotation |
| B3 | `phys.sensorGamma+` (`delta`) | |
| B4 | `phys.satKnee+` (`delta`) | |
| B5 | `phys.colorCross+` (`delta`) | |
| B6 | `brightness+` (`delta`) | display brightness (post-feedback) |
| B7 | `warp.transX.setAxis` | |
| B8 | `warp.transY.setAxis` | |

### Bottom knob row (Pan/Device) — thermal + utilities

| Knob | Bound action | Note |
| --- | --- | --- |
| P1 | `therm.amp+` (`delta`) | thermal amplitude |
| P2 | `therm.scale+` (`delta`) | |
| P3 | `therm.speed+` (`delta`) | |
| P4 | `therm.rise+` (`delta`) | |
| P5 | `therm.swirl+` (`delta`) | |
| P6 | `pattern.amount.axis` | persistent pattern amount |
| P7 | `color.hue.axis` | hue rate (continuous, separate from setAxis) |
| P8 | `fx.wet.axis` | duplicate of slider 6 for convenience |

### Top pad row — layer toggles

Pads 1–8 toggle: warp, optics, color, decay, noise, couple, external, inject.

### Bottom pad row — pattern selectors

Pads 1–8 select: H-bars, V-bars, dot, checker, rings, spiral, polka, starburst.

### Right-side column — app actions

Device → `app.fullscreen`. Mute → `preset.prev`. Solo → `preset.next`. Record → `app.screenshot`.

## Setup (60 seconds)

```bash
cat ~/workspace/crutchfield-machine/bindings.examples/launch_control_xl.ini \
  >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"

cd ~/workspace/crutchfield-machine
./feedback
```

## Factory Template 1 reference

Channel 9, the layout:

```
Send A   (top knob row)        CC 13 14 15 16 17 18 19 20
Send B   (middle knob row)     CC 29 30 31 32 33 34 35 36
Pan/Dev  (bottom knob row)     CC 49 50 51 52 53 54 55 56
Sliders                        CC 77 78 79 80 81 82 83 84
Track Focus pads (top row)     notes 41 42 43 44 57 58 59 60
Track Control pads (bot row)   notes 73 74 75 76 89 90 91 92
Device/Mute/Solo/Record col    notes 105 106 107 108
```

Templates 2–8 use channels 10–16, same CC/note layout.

## Why some knobs use `delta`

The LC XL knobs are 7-bit MIDI CCs sending an absolute 0..127 value. Two binding strategies:

- **Absolute mode** (default, no flag): the knob value IS the parameter. Useful for `setAxis` actions (saturation = knob position).
- **Delta mode** (`delta` flag): each knob change dispatches the *difference*. Useful for STEP actions like `optics.chroma+` that have no `setAxis` equivalent — the knob acts as an encoder, each turn nudges chroma up or down.

Both modes coexist in the example file. Faders are mostly absolute; knobs whose action has no `setAxis` variant are delta.

## Tips

### The bipolar slider

Slider 5 → outfade with `bipolar` flag. Slider at top (1.0) = full white fade, bottom (0.0) = full black fade, middle (0.5) = neutral. Use this to crash to black or white as a transition.

### Macro recall via templates

Switching Factory Templates 1 → 2 changes the channel from 9 to 10. If you set up bindings for both ch 9 AND ch 10 (with different action targets), one device gives you 16 banks of mappings.

```ini
# Bank 1: color-focused mappings on ch=9
color.sat.setAxis = cc:13 ch=9

# Bank 2: warp-focused mappings on ch=10 — same physical control, different action
warp.zoom.axis = cc:13 ch=10 delta
```

Hit Template button → toggle which logical bank is live.

### Using LC XL through TouchDesigner

Same routing as LC v1 — see [docs/touchdesigner/GETTING_STARTED.md](../touchdesigner/GETTING_STARTED.md). TD's MIDI In CHOP picks up the XL on channel 9 as `c9c77`, `c9c13`, `c9n41`, etc. Substitute the LC XL CC/note numbers in the Rename CHOP step.
