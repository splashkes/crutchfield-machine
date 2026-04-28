# macOS MIDI / DDJ-FLX2 notes

The macOS target has an input-only CoreMIDI backend for class-compliant
controllers. It opens the first MIDI source whose name contains the
`[midi] port = ...` substring in `bindings.ini`; the Apple default is
`DDJ-FLX2`.

Launch with message logging while mapping or debugging:

```bash
cd macOS
./feedback.app/Contents/MacOS/feedback --midi-learn
```

Or set this in `bindings.ini`:

```ini
[midi]
port = DDJ-FLX2
learn = on
```

## Binding syntax

```ini
action.name = note:NN [ch=N]
action.name = cc:NN [ch=N] [relative|delta|bipolar]
action.name = cc14:NN [ch=N] [delta|bipolar]
```

- `ch=0` or omitted means omni.
- `relative` is for Pioneer jog-wheel CC values around `0x40`.
- `delta` makes absolute knobs/faders send parameter changes instead of
  snapping parameters to hardware position.
- `bipolar` remaps an absolute `0..1` control to `-1..+1`; the default
  crossfader binding uses this for output fade.
- `shifted` requires the DDJ-FLX2 Shift button to be held. The observed
  hardware sends Shift as deck note `63`, while pads can remain on normal
  channels `8` and `10`; the default map supports both this software-shift
  path and the documented shifted pad channels `9` and `11`.

## DDJ-FLX2 channel layout

From AlphaTheta/Pioneer DJ's DDJ-FLX2 MIDI message list:

- Deck 1 controls: channel 1.
- Deck 2 controls: channel 2.
- Global mixer/effect controls: channel 7.
- Deck 1 pads: channel 8, shifted pads channel 9.
- Deck 2 pads: channel 10, shifted pads channel 11.

The FLX2 sends high-resolution faders/knobs as 14-bit CC pairs:
`cc14:N` means MSB CC `N` plus LSB CC `N+32`.

## Default map

The Apple build installs these MIDI defaults before reading
`bindings.ini`. If a controller is not connected they are inert.

Jogs:

- Left platter: rotation.
- Right platter: zoom.
- Left wheel side: translate X.
- Right wheel side: translate Y.

Mixer:

- Tempo sliders: rotation and zoom nudges.
- Deck 1 EQ high/mid/low: saturation, hue rate, gamma.
- Deck 2 EQ high: output fade, center neutral, left black, right white.
- Deck 2 EQ mid/low: blur X and decay.
- CFX CH1 knob: persistent shape count, from 1 to 16.
- CFX CH2 knob: coupling.
- Channel faders: external amount and thermal amplitude.
- Master level: contrast.
- Headphones level: blur Y.
- Crossfader: external video blend, left dry/internal, right camera/external.

Buttons:

- Play/Pause: pause.
- Deck 1 Cue: inject hold.
- Deck 2 Cue: clear.
- Channel cue buttons: external and thermal layer toggles.
- Beat Sync on either deck: BPM tap.
- Smart Fader: BPM sync toggle.
- Master Cue: help overlay.

Pads and LEDs:

- Deck 1 pads 1-4: hold persistent shape injects:
  triangle, star, circle, square.
- Deck 1 pads 1-4 also keep their original pattern selection behavior.
- Deck 1 pad 5: select the gradient inject pattern.
- Deck 1 pads 6-8: VFX-1 previous, next, off.
- Shift + deck 1 pads: VFX-1 quick-select bank. Pads 1-8 are:
  off, VCR, Pixel, Strobe, Posterize, Negative, Mirror-HV, PinP.
  While deck-1 Shift is held, the current quick-select filter is lit.
- Deck 2 pads 1-8: toggle the performance layers and receive LED state:
  warp, optics, color, decay, noise, couple, external, inject.
- Shift + deck 2 pads: physics, thermal, quality cycles, BPM flash/decay toggles.

The LED feedback is simple on/off Note-On feedback. Deck-2 layer LEDs use
channel 10. Shift-revealed deck-1 VFX LEDs use channels 8 and 9 so both the
observed software-shift behavior and the documented shifted pad channel are
covered.

## Backend notes

CoreMIDI callbacks run off the main thread. `macOS/midi_coremidi.mm`
only queues compact MIDI messages; `Input::pollMidi` drains that queue
on the render thread and dispatches through the same action callback as
keyboard and gamepad input.

LED feedback back to the controller is deliberately deferred. The first
usable pass is input-only so the control mapping can settle before the
abstraction grows an output path.
