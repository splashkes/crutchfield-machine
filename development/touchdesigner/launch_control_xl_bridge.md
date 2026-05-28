# Launch Control XL → TouchDesigner → Crutchfield

Use TouchDesigner as a MIDI-to-OSC bridge for the Novation Launch
Control XL (or any class-compliant MIDI controller). This adds
smoothing, blending, scaling, and automation between the physical
controller and Crutchfield.

If you don't need any of that, just plug the LC XL into the Mac and
use `bindings.examples/launch_control_xl.ini` directly. This document
is for when you want TD in the middle.

## Network

```
[ Launch Control XL ]
         │ USB MIDI
         ▼
[ MIDI In CHOP ]                              ─ ch=9 (Factory Template 1)
         │
         ▼
[ Select CHOP ]   ← pick the CCs/notes you care about
         │
         ▼
[ Math / Filter CHOPs ]   ← optional smoothing, scaling, deadzones
         │
         ▼
[ Rename CHOP ]   ← give each channel an OSC-friendly name
         │
         ▼
[ OSC Out CHOP ]  →  127.0.0.1 : 7700
```

## Setup steps

1. Plug LC XL into your Mac.
2. **MIDI In CHOP**: set `Device` to `Launch Control XL`. Channels
   appear as `c1cN` (channel 1 CCs) etc. Factory Template 1 sends on
   channel 9 so you'll see `c9c13`, `c9c77`, `c9n41`, etc.
3. **Select CHOP**: pick the source channels. For the slider bank:
   `c9c77 c9c78 c9c79 c9c80 c9c81 c9c82 c9c83 c9c84`.
4. **Rename CHOP**: name the channels to match OSC addresses you want
   to send. Example mapping:
   | LC XL source | TD channel name      | OSC address       |
   | ------------ | -------------------- | ----------------- |
   | `c9c77` (slider 1) | `cma/decay`     | `/cma/decay`      |
   | `c9c78` (slider 2) | `cma/external`  | `/cma/external`   |
   | `c9c13` (knob A1)  | `cma/sat`       | `/cma/sat`        |
   | `c9n41` (pad 1)    | `cma/layer/warp`| `/cma/layer/warp` |
5. **Math CHOP** (optional): MIDI CC values arrive as 0..1 (or 0..127
   on the raw side depending on TD version). Scale/clamp as needed.
6. **OSC Out CHOP**: address `127.0.0.1`, port `7700`, send-every-frame
   on.
7. **Verify**: in Crutchfield, launch with `--osc-listen --osc-learn`,
   move an LC XL slider. The address should print on the Crutchfield
   console.
8. **Wire bindings**: append
   `bindings.examples/crutchfield_touchdesigner.ini` to your
   `bindings.ini` (it covers all the addresses listed in the map).
   Restart Crutchfield.

## Why use TD in the middle

Scenarios that justify the extra hop:

- **Blend two controllers**: LC XL + a Korg nanoKontrol → Math CHOP
  weighted blend → one OSC stream into Crutchfield.
- **Automation lanes**: TD's CHOP timeline records LC XL movements
  and plays them back. Perfect for show-control.
- **Audio-reactive overlay**: an Audio Analysis CHOP modulates the LC
  XL's slider values before they reach Crutchfield. The fader becomes
  a "scene intensity" with audio drive layered on top.
- **Mode banks beyond 8**: LC XL Templates 1-8 give you 8 banks; route
  them into TD and you can stack more (`Switch CHOP` per template).
- **Smoothing**: MIDI 7-bit CC is jumpy. A `Lag CHOP` between MIDI In
  and OSC Out fixes it.

## Skip TD if

- You want lowest-latency direct hardware → app
- You don't need blending or automation
- You're fine editing `bindings.ini` to remap

In that case use `bindings.examples/launch_control_xl.ini` directly.
