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
- Smart Fader: FX wet mode toggle.
- Master Cue (held): alternate pad bank — see "Master Cue bank" below.

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

Master Cue bank (hold Master Cue + tap a deck-2 pad):

- Pads 1-4: noise archetype select (white / pink / heavy-static / VCR).
  Currently-selected archetype is lit.
- Pad 5: noise layer toggle (lit when on).
- Pad 6: inject layer toggle (lit when on).
- Pad 7: hi-res screenshot — 16-tap rotated-grid supersample at 2× sim
  resolution (writes `~/Pictures/crutchfield/shot_hires_*.png`). Always lit.
- Pad 8: native-resolution screenshot (writes `shot_*.png`). Always lit.

The LED feedback is simple on/off Note-On feedback. Deck-2 layer LEDs use
channel 10. Shift-revealed deck-1 VFX LEDs use channels 8 and 9 so both the
observed software-shift behavior and the documented shifted pad channel are
covered.

## Backend notes

CoreMIDI callbacks run off the main thread. `macOS/midi_coremidi.mm`
only queues compact MIDI messages; `Input::pollMidi` drains that queue
on the render thread and dispatches through the same action callback as
keyboard and gamepad input.

LED feedback is wired: the CoreMIDI backend opens an output port to the
same DDJ destination, and `Input::sendMidiNote` pushes Note-On updates.
`sync_ddj_layer_leds()` and `sync_ddj_filter_leds()` in `main.cpp` are
called on layer toggle, VFX slot change, preset load, and on the
rising edge of MIDI connect and each shift state. Velocities are
simple on/off (`0x7F` / `0x00`). The deck-2 layer LEDs use channel 10;
shift-revealed deck-1 VFX LEDs are mirrored on channels 8 and 9.
