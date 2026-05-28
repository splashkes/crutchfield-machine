# TouchDesigner integration — full walkthrough

Build a TouchDesigner network that drives Crutchfield Machine over OSC. From zero to a fully-mapped control surface in about 20 minutes.

## What you'll build

```
┌─────────────────────────────────────────────┐
│  TouchDesigner panel                        │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐        │
│  │decay │ │ sat  │ │outfde│ │ blur │        │
│  │ ===  │ │ ===  │ │ ===  │ │ ===  │        │
│  │  ○   │ │  ○   │ │  ○   │ │  ○   │        │
│  └──────┘ └──────┘ └──────┘ └──────┘        │
│                                             │
│  ┌─┐ ┌─┐ ┌─┐ ┌─┐  Layer toggles             │
│  └─┘ └─┘ └─┘ └─┘  (Buttons)                 │
│                                             │
│  ┌────────┐  Take screenshot                │
│  └────────┘                                 │
└─────────────┬───────────────────────────────┘
              │ OSC Out CHOP
              │ 127.0.0.1:7700
              ▼
┌─────────────────────────────────────────────┐
│  Crutchfield Machine                        │
│  --osc-listen                               │
│  bindings.ini [osc] section pointing each   │
│  /cma/* address at the right action         │
└─────────────────────────────────────────────┘
```

## Prerequisites

- TouchDesigner installed (free non-commercial license is fine)
- Crutchfield built and running on the same machine
- `bindings.examples/crutchfield_touchdesigner.ini` appended to your Crutchfield bindings.ini

## Step 1 — Wire up the OSC output (3 minutes)

In TouchDesigner, create a new project. Drop these operators:

### 1a. OSC Out CHOP

In an empty network, press `Tab` and search for "OSC Out". Drop it on the canvas.

Parameters:
- **Network Address**: `127.0.0.1` (localhost; change for remote machines)
- **Network Port**: `7700`
- **Send Mode**: `Send Channel Values`
- **Send Every Frame**: `On`

### 1b. Constant CHOP as source

Drop a **Constant CHOP**. Set:
- **Name 0**: `cma/decay`
- **Value 0**: `0.8`
- **Name 1**: `cma/sat`
- **Value 1**: `0.5`

Wire the Constant CHOP into the OSC Out CHOP.

### 1c. Test reception

In a terminal:

```bash
cd ~/workspace/crutchfield-machine
./feedback --osc-listen --osc-learn
```

You should see in the Crutchfield console:

```
[osc] listening on UDP port 7700
[osc-learn] /cma/decay f=0.8000
[osc-learn] /cma/sat f=0.5000
```

These print every frame (60×/sec). The decay parameter in Crutchfield should now hold at 0.8.

If you don't see learn lines: see [docs/osc/TROUBLESHOOTING.md](../osc/TROUBLESHOOTING.md).

## Step 2 — Add interactive sliders (5 minutes)

### 2a. Create a Panel COMP

In a new network area, drop a **Panel COMP** (a container for UI). Inside the panel, add:

- Two **Slider COMPs** for `decay` and `sat`
- A **Container COMP** with two **Button COMPs** for layer toggles

### 2b. Wire Sliders to Constant CHOP

The Slider COMP outputs a `panel/v` channel (its current value, 0..1).

In your Constant CHOP, change Value 0 to an expression that reads the slider:

```
op('panel1/slider1').par.value0
```

Or use a more idiomatic approach with a **Select CHOP**:

```
[ Slider1 COMP ]    [ Slider2 COMP ]
       │                   │
       ▼                   ▼
   [ Select CHOP ] ←─ pick the slider's `v` channel
       │                   │
       ▼                   ▼
   [ Rename CHOP ]    [ Rename CHOP ]
   "v" → "cma/decay"  "v" → "cma/sat"
       └──────────┬────────┘
                  ▼
            [ Merge CHOP ]
                  │
                  ▼
            [ OSC Out CHOP ]
```

### 2c. Test live

Move the slider. Crutchfield's decay parameter follows.

## Step 3 — Layer toggle buttons (5 minutes)

Layer toggles are different from sliders: you want a press to fire `1.0` once, not hold the button down forever.

### 3a. Button COMP → Trigger CHOP

Wire each Button COMP into a **Trigger CHOP**. Parameters:
- **Trigger**: rising edge
- **Output Length**: 0.05 sec (just long enough to be seen by Crutchfield's frame loop)

### 3b. Rename + merge into OSC Out

```
[ Button COMP "warp" ] → [ Trigger CHOP ] → [ Rename ] "v" → "cma/layer/warp"
[ Button COMP "noise" ] → [ Trigger CHOP ] → [ Rename ] "v" → "cma/layer/noise"
[ Button COMP "decay" ] → [ Trigger CHOP ] → [ Rename ] "v" → "cma/layer/decay"
                                                        │
                                                        ▼
                                                  [ Merge CHOP ]
                                                        │
                                                        ▼
                                            into your main OSC Out CHOP
```

### 3c. Test

Press a layer button. Crutchfield's layer toggles. Press again: it toggles back.

## Step 4 — Screenshot button (2 minutes)

Same pattern as layer toggle:

```
[ Button COMP "shot" ] → [ Trigger CHOP ] → [ Rename ] "v" → "cma/shot"
                                                        │
                                                        ▼
                                                  into main OSC Out CHOP
```

Crutchfield bindings include:

```ini
app.screenshot = osc:/cma/shot
```

Press → file appears in `~/Pictures/crutchfield/`.

## Step 5 — Audio-reactive smoothing (optional, 5 minutes)

Replace the manual decay slider with an audio envelope.

```
[ Audio Device In CHOP ]   ← system audio (Aggregate Device or Loopback)
        │
        ▼
[ Analyze CHOP ]           ← Function: RMS Power, Window: 0.2
        │
        ▼
[ Lag CHOP ]               ← Lag 1: 0.05, Lag 2: 0.15 (smooth tail)
        │
        ▼
[ Math CHOP ]              ← Multiply: 0.7, Add: 0.3 (remap to 0.3..1.0 range)
        │
        ▼
[ Rename ] "level" → "cma/decay"
        │
        ▼
into main OSC Out CHOP
```

Music plays → decay reacts to loudness. Loud sections are jittery (low decay), quiet sections linger (high decay).

## Step 6 — Save the project (.toe)

`File > Save As` → store it next to Crutchfield, e.g. `~/workspace/crutchfield-machine/development/touchdesigner/crutchfield_control.toe`.

(We don't ship a `.toe` because the format is binary and version-fragile across TD releases. Build your own once and reuse.)

## The canonical OSC namespace

Use these addresses to match the ready-made bindings (in `bindings.examples/crutchfield_touchdesigner.ini`):

### Continuous (axis) addresses

```
/cma/decay             dyn.decay.axis
/cma/external          dyn.external.axis
/cma/noise             dyn.noise.axis
/cma/couple            dyn.couple.setAxis
/cma/outfade           outfade.axis (bipolar — center at 0)
/cma/fx/wet            fx.wet.axis
/cma/sat               color.sat.setAxis
/cma/hue               color.hue.setAxis
/cma/gamma             color.gamma.setAxis
/cma/contrast          color.contrast.setAxis
/cma/blur/x            optics.blurX.setAxis
/cma/blur/y            optics.blurY.setAxis
/cma/zoom              warp.zoom.axis (bipolar)
/cma/theta             warp.theta.axis (bipolar)
/cma/trans/x           warp.transX.setAxis (bipolar)
/cma/trans/y           warp.transY.setAxis (bipolar)
/cma/shape/count       shape.count.axis
/cma/shape/size        shape.size.axis
/cma/shape/rot         shape.rot.axis
/cma/pattern/amount    pattern.amount.axis
```

### Layer toggles

```
/cma/layer/warp        layer.warp
/cma/layer/optics      layer.optics
/cma/layer/color       layer.color
/cma/layer/decay       layer.decay
/cma/layer/noise       layer.noise
/cma/layer/couple      layer.couple
/cma/layer/external    layer.external
/cma/layer/inject      layer.inject
/cma/layer/physics     layer.physics
/cma/layer/thermal     layer.thermal
```

### Pattern selectors

```
/cma/pattern/hbars     pattern.hbars
/cma/pattern/vbars     pattern.vbars
/cma/pattern/dot       pattern.dot
/cma/pattern/checker   pattern.checker
/cma/pattern/rings     pattern.rings
/cma/pattern/spiral    pattern.spiral
/cma/pattern/polka     pattern.polka
/cma/pattern/starburst pattern.starburst
/cma/inject/hold       inject.hold (trigger: press AND release fire)
```

### App actions

```
/cma/preset/next       preset.next
/cma/preset/prev       preset.prev
/cma/shot              app.screenshot
/cma/shot/hires        app.screenshot.hires
/cma/fullscreen        app.fullscreen
/cma/clear             app.clear
/cma/pause             app.pause
```

47 addresses. See [development/touchdesigner/crutchfield_osc_map.json](../../development/touchdesigner/crutchfield_osc_map.json) for the same map as importable JSON (each entry has address, action name, kind, suggested min/max).

## Network topology tips

### Multiple inputs blended

```
[ Slider COMP ]      [ Audio envelope ]      [ LFO CHOP ]
     │                       │                     │
     └───────────┬───────────┴─────────────────────┘
                 ▼
           [ Math CHOP ]              ← combine = Add, then Limit 0..1
                 │
                 ▼
           [ OSC Out CHOP ]
```

### Smoothing jittery sources

```
[ MIDI In CHOP ]                  ← 7-bit MIDI is jumpy
        │
        ▼
[ Lag CHOP ]   Lag 1: 0.02         ← exponential smoothing
        │
        ▼
[ OSC Out CHOP ]
```

### Hold-to-fire latching

If your Trigger CHOP fires once but you want the action to stay "on" until released, switch to a Button COMP with **Trigger Type: Hold** and feed the raw value:

```
[ Button COMP, Hold mode ] → outputs 1 while held, 0 when released
        │
        ▼
[ Rename ] "v" → "cma/inject/hold"
```

Crutchfield's `inject.hold = osc:/cma/inject/hold` fires on both edges (press + release) so this Just Works.

## Multi-machine setup

Render mac (running Crutchfield):

```bash
./feedback --osc-listen 7700
```

Control laptop (running TouchDesigner):

In OSC Out CHOP, set **Network Address** to the render mac's IP (e.g. `192.168.1.50`).

The listener binds INADDR_ANY so it accepts from any LAN source. Open UDP 7700 in your firewall if needed.

## Save / load presets in TD

TD's **CHOP Execute DAT** can save the current panel state on a button press and recall it later. Useful for "save scene 1", "recall scene 2" workflows. Or simpler: use TD's Presets feature on the Constant CHOP that drives OSC Out.

If you want Crutchfield's own preset system instead: bind `/cma/preset/next` and `/cma/preset/prev` to TD buttons. Crutchfield cycles through its built-in preset library (`presets/01_default.ini`, etc.).

## Beyond this guide

- Audio-reactive recipes → [docs/osc/COOKBOOK.md](../osc/COOKBOOK.md) recipe 1
- LC v1 as MIDI bridge → [docs/touchdesigner/launch_control_xl_bridge.md](../../development/touchdesigner/launch_control_xl_bridge.md)
- Wire-level OSC details → [docs/osc/PROTOCOL.md](../osc/PROTOCOL.md)
- Architecture / threading → [docs/osc/ARCHITECTURE.md](../osc/ARCHITECTURE.md)
