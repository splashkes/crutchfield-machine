# TouchDesigner ↔ Crutchfield Machine

This directory holds OSC integration assets for TouchDesigner.

## What's here

- `crutchfield_osc_map.json` — canonical list of OSC addresses the
  ready-to-use `bindings.examples/crutchfield_touchdesigner.ini`
  expects. Importable into TD to auto-create matching CHOP channels.
- `BUILD_NETWORK.md` — step-by-step instructions to assemble the
  `.toe` network manually. `.toe` files are binary, so we ship the
  recipe instead of the file.
- `launch_control_xl_bridge.md` — recipe for using TouchDesigner as a
  MIDI→OSC bridge for the Novation Launch Control XL (or any class-
  compliant MIDI controller).

## TL;DR

1. In TouchDesigner, drop an **OSC Out CHOP**.
2. Set `Network Address = 127.0.0.1`, `Network Port = 7700`.
3. Feed it a CHOP whose channel names match the addresses in
   `crutchfield_osc_map.json` (e.g. channel `cma/decay` → address
   `/cma/decay`).
4. Append `bindings.examples/crutchfield_touchdesigner.ini` to your
   Crutchfield `bindings.ini`. Restart Crutchfield. You're live.

See `BUILD_NETWORK.md` for the full visual recipe.
