# Action catalogue

Every action you can bind to OSC (or keyboard / gamepad / MIDI), grouped by section. **181 entries total.** Generated from `./feedback --list-actions`.

## How to read this table

- **Action name**: the literal string you put on the left of `=` in a binding line.
- **Description**: what the action does. Lifted from `ActionInfo::desc` in `input.cpp`.
- **Kind**: inferred from suffix conventions:
  - `axis` → continuous (axis-shaped); pair with `osc:/...` (auto-selects `SRC_OSC_F`)
  - `step` → continuous nudge (`+`/`-`); pair with `osc:/...` or `osct:/...`
  - `discrete` → one-shot fire; pair with `osc:/...` or `osct:/...`
  - `trigger` → press + release edges; pair with `osc:/...` or `osct:/...`
  - `?` → couldn't infer; check description for hints, or run `./feedback --list-actions | grep <name>` and look at the group context

## Quick reference: which OSC spec to use

| Action kind | Spec to use | Why |
| --- | --- | --- |
| `axis` (decay, sat, gamma, …) | `osc:/...` (optionally with `bipolar`, `delta`, `scale=X`) | continuous values |
| `step` (chroma+, therm.amp+) | `osc:/...` with `delta` flag | knob nudges instead of absolute jumps |
| `discrete` (layer.X, preset.next) | `osc:/...` (auto-selects trigger semantics) | press > 0.5 = fire |
| `trigger` (inject.hold) | `osc:/...` (or `osct:/...` to force) | press + release both fire |

## All actions, grouped


## Layers

| Action name | Description | Kind |
| --- | --- | --- |
| `layer.warp` | toggle warp | discrete |
| `layer.optics` | toggle optics | discrete |
| `layer.gamma` | toggle gamma | discrete |
| `layer.color` | toggle color | discrete |
| `layer.contrast` | toggle contrast | discrete |
| `layer.decay` | toggle decay | discrete |
| `layer.noise` | toggle noise | discrete |
| `layer.couple` | toggle couple | discrete |
| `layer.external` | toggle external (camera) | discrete |
| `layer.inject` | toggle inject | discrete |
| `layer.physics` | toggle physics | discrete |
| `layer.thermal` | toggle thermal | discrete |
| `layer.cursor.up` | cursor prev | discrete |
| `layer.cursor.dn` | cursor next | discrete |
| `layer.toggleArmed` | toggle armed layer | discrete |

## Warp

| Action name | Description | Kind |
| --- | --- | --- |
| `warp.zoom+` | zoom + | step |
| `warp.zoom-` | zoom - | step |
| `warp.theta+` | rotation + | step |
| `warp.theta-` | rotation - | step |
| `warp.trans.left` | translate -X | ? |
| `warp.trans.right` | translate +X | ? |
| `warp.trans.up` | translate -Y | ? |
| `warp.trans.down` | translate +Y | ? |
| `warp.zoom.axis` | zoom (axis) | axis |
| `warp.theta.axis` | rotation (axis) | axis |
| `warp.transX.axis` | translate X (axis) | axis |
| `warp.transY.axis` | translate Y (axis) | axis |
| `warp.transX.setAxis` | translate X setpoint | axis |
| `warp.transY.setAxis` | translate Y setpoint | axis |

## Optics

| Action name | Description | Kind |
| --- | --- | --- |
| `optics.chroma+` | chroma aberration + | step |
| `optics.chroma-` | chroma aberration - | step |
| `optics.blurX+` | blur X + | step |
| `optics.blurX-` | blur X - | step |
| `optics.blurY+` | blur Y + | step |
| `optics.blurY-` | blur Y - | step |
| `optics.blurAng+` | blur angle + | step |
| `optics.blurAng-` | blur angle - | step |
| `optics.blurX.setAxis` | blur/sharp X setpoint | axis |
| `optics.blurY.setAxis` | blur/sharp Y setpoint | axis |

## Color

| Action name | Description | Kind |
| --- | --- | --- |
| `color.gamma+` | gamma + | step |
| `color.gamma-` | gamma - | step |
| `color.hueRate+` | hue rate + | step |
| `color.hueRate-` | hue rate - | step |
| `color.sat+` | saturation + | step |
| `color.sat-` | saturation - | step |
| `color.contrast+` | contrast + | step |
| `color.contrast-` | contrast - | step |
| `color.gamma.setAxis` | gamma setpoint | axis |
| `color.hue.setAxis` | hue rate setpoint | axis |
| `color.sat.setAxis` | saturation setpoint | axis |
| `color.contrast.setAxis` | contrast setpoint | axis |
| `color.hue.axis` | hue rate (axis) | axis |

## Dynamics

| Action name | Description | Kind |
| --- | --- | --- |
| `dyn.decay+` | decay + | step |
| `dyn.decay-` | decay - | step |
| `dyn.noise+` | noise + | step |
| `dyn.noise-` | noise - | step |
| `dyn.couple+` | couple + | step |
| `dyn.couple-` | couple - | step |
| `dyn.external+` | external (cam) + | step |
| `dyn.external-` | external (cam) - | step |
| `dyn.couple.setAxis` | couple setpoint | axis |
| `dyn.noise.axis` | noise amount (axis) | axis |
| `dyn.decay.axis` | decay setpoint (axis) | axis |
| `dyn.external.axis` | external (axis) | axis |
| `fx.wet.axis` | effect wet mix (axis) | axis |
| `fx.wetMode` | toggle crossfader wet mode | ? |

## Physics

| Action name | Description | Kind |
| --- | --- | --- |
| `phys.invert` | invert toggle | ? |
| `phys.sensorGamma+` | sensor gamma + | step |
| `phys.sensorGamma-` | sensor gamma - | step |
| `phys.satKnee+` | sat knee + | step |
| `phys.satKnee-` | sat knee - | step |
| `phys.colorCross+` | color cross + | step |
| `phys.colorCross-` | color cross - | step |

## Thermal

| Action name | Description | Kind |
| --- | --- | --- |
| `therm.amp+` | amplitude + | step |
| `therm.amp-` | amplitude - | step |
| `therm.scale+` | scale + | step |
| `therm.scale-` | scale - | step |
| `therm.speed+` | speed + | step |
| `therm.speed-` | speed - | step |
| `therm.rise+` | rise + | step |
| `therm.rise-` | rise - | step |
| `therm.swirl+` | swirl + | step |
| `therm.swirl-` | swirl - | step |

## Inject

| Action name | Description | Kind |
| --- | --- | --- |
| `pattern.hbars` | pattern: H-bars | discrete |
| `pattern.vbars` | pattern: V-bars | discrete |
| `pattern.dot` | pattern: dot | discrete |
| `pattern.checker` | pattern: checker | discrete |
| `pattern.grad` | pattern: gradient | discrete |
| `pattern.noise` | pattern: noise field | discrete |
| `pattern.rings` | pattern: concentric rings | discrete |
| `pattern.spiral` | pattern: spiral | discrete |
| `pattern.polka` | pattern: polka dots | discrete |
| `pattern.starburst` | pattern: starburst | discrete |
| `pattern.bouncer` | pattern: bouncer (10s animated box) | discrete |
| `shape.triangle.hold` | shape: triangle hold | trigger |
| `shape.star.hold` | shape: star hold | trigger |
| `shape.circle.hold` | shape: circle hold | trigger |
| `shape.square.hold` | shape: square hold | trigger |
| `inject.hold` | inject (hold) | trigger |
| `pattern.cursor.up` | pattern prev | discrete |
| `pattern.cursor.dn` | pattern next | discrete |
| `pattern.amount.axis` | persistent pattern amount | axis |
| `shape.count.axis` | shape count (axis) | axis |
| `shape.size.axis` | shape size (axis) | axis |
| `shape.rot.axis` | shape rotation (axis) | axis |

## Output

| Action name | Description | Kind |
| --- | --- | --- |
| `outfade.up` | fade toward white | ? |
| `outfade.down` | fade toward black | ? |
| `outfade.axis` | fade (axis -1..+1) | axis |
| `brightness+` | display brightness + | step |
| `brightness-` | display brightness - | step |

## VFX-1

| Action name | Description | Kind |
| --- | --- | --- |
| `vfx1.next` | slot 1: next effect | ? |
| `vfx1.prev` | slot 1: prev effect | ? |
| `vfx1.off` | slot 1: off | ? |
| `vfx1.param+` | slot 1: param + | step |
| `vfx1.param-` | slot 1: param - | step |
| `vfx1.bsrc` | slot 1: cycle B-source | ? |
| `vfx1.pad0` | slot 1: pad bank 1 | ? |
| `vfx1.pad1` | slot 1: pad bank 2 | ? |
| `vfx1.pad2` | slot 1: pad bank 3 | ? |
| `vfx1.pad3` | slot 1: pad bank 4 | ? |
| `vfx1.pad4` | slot 1: pad bank 5 | ? |
| `vfx1.pad5` | slot 1: pad bank 6 | ? |
| `vfx1.pad6` | slot 1: pad bank 7 | ? |
| `vfx1.pad7` | slot 1: pad bank 8 | ? |

## VFX-2

| Action name | Description | Kind |
| --- | --- | --- |
| `vfx2.next` | slot 2: next effect | ? |
| `vfx2.prev` | slot 2: prev effect | ? |
| `vfx2.off` | slot 2: off | ? |
| `vfx2.param+` | slot 2: param + | step |
| `vfx2.param-` | slot 2: param - | step |
| `vfx2.bsrc` | slot 2: cycle B-source | ? |

## Quality

| Action name | Description | Kind |
| --- | --- | --- |
| `q.blur` | cycle blur kernel | ? |
| `q.ca` | cycle CA sampler | ? |
| `q.noise` | cycle noise type | ? |
| `q.fields` | cycle coupled fields | ? |
| `q.pixelate` | cycle pixelate style | ? |
| `q.pixelateBleed` | cycle pixelate bleed (CRT feel) | ? |
| `q.pixelateBurnReseed` | reroll burned pixel pattern (preset 'burned') | ? |
| `q.sphere` | toggle sphere topology/display | ? |
| `q.cursor.up` | cursor prev | ? |
| `q.cursor.dn` | cursor next | ? |
| `q.cycleArmed` | cycle armed quality | discrete |
| `q.noise.white` | noise type: white | ? |
| `q.noise.pink` | noise type: pink | ? |
| `q.noise.grain` | noise type: grain | ? |
| `q.noise.scanline` | noise type: scanline | ? |

## BPM

| Action name | Description | Kind |
| --- | --- | --- |
| `bpm.tap` | tap tempo | ? |
| `bpm.sync` | BPM sync on/off | ? |
| `bpm.div` | cycle beat division (x2/1/½/¼) | ? |
| `bpm.injectOnBeat` | toggle inject-on-beat | ? |
| `bpm.strobeLock` | toggle strobe rate lock | ? |
| `bpm.vfxCycle` | toggle vfx auto-cycle on beat | ? |
| `bpm.flash` | toggle fade-flash on beat | ? |
| `bpm.decayDip` | toggle decay-dip on beat | ? |
| `bpm.hueJump` | toggle hue-jump on beat | ? |
| `bpm.hueJumpStep+` | hue-jump step + | step |
| `bpm.hueJumpStep-` | hue-jump step - | step |
| `bpm.invert` | toggle beat-driven invert flip | ? |
| `bpm.invertDiv+` | invert flip divisor + | step |
| `bpm.invertDiv-` | invert flip divisor - | step |
| `music.installMidiDriver` | install MIDI driver (Windows) | ? |
| `music.next` | next music preset | ? |
| `music.prev` | prev music preset | ? |
| `music.playpause` | music play/pause | ? |

## App

| Action name | Description | Kind |
| --- | --- | --- |
| `app.clear` | clear fields | discrete |
| `app.pause` | pause/resume | discrete |
| `app.help` | help toggle | discrete |
| `help.up` | help cursor up | ? |
| `help.down` | help cursor down | ? |
| `help.enter` | help drill in | ? |
| `help.back` | help back / close | ? |
| `app.reloadShaders` | reload shaders | discrete |
| `app.fullscreen` | fullscreen toggle | discrete |
| `rec.toggle` | recording start/stop | ? |
| `app.screenshot` | screenshot (PNG, sim resolution, no HUD) | discrete |
| `app.screenshot.hires` | screenshot (PNG, supersampled K x sim, no HUD) | discrete |
| `preset.save` | preset save | discrete |
| `preset.next` | preset next | discrete |
| `preset.prev` | preset prev | discrete |
| `app.helpStdout` | print help to stdout | discrete |
| `app.quit` | quit | discrete |
| `ddj.bank.hold` | DDJ alternate pad bank | trigger |
