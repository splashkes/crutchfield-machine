# feedback.exe — reference

Complete user-facing reference. Best bootup settings, every keybind
with its layer + parameter, full gamepad layout, recording workflow,
music authoring walkthrough, and troubleshooting.

For the one-page printable sheet (keys only, no prose) see
[CHEAT_SHEET.md](CHEAT_SHEET.md). For the *why* of any of this see
[development/LAYERS.md](development/LAYERS.md),
[development/ARCHITECTURE.md](development/ARCHITECTURE.md), or
[development/DESIGN.md](development/DESIGN.md).

---

## Best bootup settings

Pick one line, open a shell in the project folder, run it.

### Quality-first (what to try on a beefy GPU)

```
./feedback.exe --fullscreen --precision 32 --blur-q 2 --ca-q 2 --fields 4 \
               --preset 02_rainbow_spiral
```

- `--fullscreen` — borderless, native monitor res. Sim matches display.
- `--precision 32` — full float feedback buffer. The whole pipeline
  runs unclamped; HDR headroom survives contrast overshoot and CA.
- `--blur-q 2` — 25-tap Gaussian. Smooth diffuse defocus.
- `--ca-q 2` — 8-sample wavelength-weighted chromatic aberration.
  Sharp colour fringes pairing well with pixelate at 4:4:4 capture.
- `--fields 4` — four Kaneko-coupled feedback fields with mirrored
  theta and hueRate. Rich cross-channel dynamics.
- `--preset 02_rainbow_spiral` — fast-rotating colour preset.

### Balanced (every machine)

```
./feedback.exe --fullscreen --precision 16
```

Half-float feedback. Visually near-identical to 32-bit on most scenes
but ~2× faster on weaker GPUs.

### First-time / no flags

```
./feedback.exe
```

Drops into an interactive picker (`1` default · `2` fullscreen · `3`
gallery/auto-demo · `4` 4K · `5` 8-bit study · `6` load preset · `7`
music setup). Good when double-clicked from Explorer.

### Unattended / gallery

```
./feedback.exe --fullscreen --demo
```

Cycles through curated presets every 30 s and fires random injects
every 8 s. Walk away, come back, still beautiful.

### Lightweight (laptop / IGP)

```
./feedback.exe --fullscreen --sim-res 1920x1080 --precision 16 \
               --blur-q 0 --ca-q 0
```

### What happens on boot regardless of flags

- A **random inject pattern** fires at `inject = 1.0` once (the
  normal fadeout takes it out over ~20 frames). No more black screen.
- All 12 layers enabled by default. Physics and thermal have
  "off-equivalent" parameter defaults so this doesn't explode.
- `bpmHueJump` is on at step 12 (1/8 rotation per beat). When you
  engage BPM sync, colours sweep gently on each beat.

### What you should do first after it's running

1. Press **`H`** — help panel opens in-window, drill-down by section.
2. Press **`Tab`** a few times at the tempo you want, then **`Ctrl+Tab`**
   to lock BPM sync on. Music now drives the hue sweep.
3. Press **`` ` ``** (backtick) to start an EXR recording. Press again
   to stop. On exit the app offers to encode to HEVC MP4 at whatever
   quality preset you choose.

---

## Full keyboard layout

Hold **`Shift`** for 20× coarse step on any parameter nudge.

### Layer toggles

| Key | Layer | What it does when on |
|---|---|---|
| `F1` | warp | Geometric zoom + rotation + pivot + translate |
| `F2` | optics | Anisotropic blur + chromatic aberration |
| `F3` | gamma | Linear-light bracketing for analog-stage maths |
| `F4` | color | HSV hue rotation + saturation |
| `F5` | contrast | S-curve contrast around mid-grey |
| `F6` | decay | Per-frame exponential bleed toward black |
| `F7` | noise | Sensor-floor noise (5 archetypes — see `Home`) |
| `F8` | couple | Kaneko blend with partner field |
| `F9` | external | Live camera mix (if Media Foundation finds one) |
| `F10` | inject | Pattern injection layer (needed for Space / 1-0 keys) |
| `Ins` | physics | Crutchfield camera-side knobs (sensor γ, knee, xtalk) |
| `PgDn` | thermal | Air-turbulence UV shimmer |

Toggling a layer off leaves its parameters untouched — values are
preserved for when you turn it back on. If you nudge a value while
its layer is off, the HUD will remind you ("*contrast set — layer
off, F5 to activate*").

### Parameter nudges (hold `Shift` for ×20)

| Key ± | Parameter | Layer |
|---|---|---|
| `Q` / `A` | Zoom | warp (F1) |
| `W` / `S` | Theta (rotation) | warp |
| `←/→/↑/↓` | Translate X / Y | warp |
| `[` / `]` | Chroma (CA) | optics (F2) |
| `;` / `'` | Blur X | optics |
| `,` / `.` | Blur Y | optics |
| `-` / `=` | Blur angle | optics |
| `G` / `B` | Gamma | gamma (F3) |
| `E` / `D` | Hue rate | color (F4) |
| `R` / `F` | Saturation | color |
| `T` / `Y` | Contrast | contrast (F5) |
| `U` / `J` | Decay | decay (F6) |
| `N` / `M` | Noise amp | noise (F7) |
| `K` / `I` | Couple amount | couple (F8) |
| `O` / `L` | External (camera mix) | external (F9) |
| `V` | Invert toggle | (out-of-layer; always acts) |
| `X` / `Z` | Sensor gamma | physics (Ins) |
| `8` / `7` | Saturation knee | physics |
| `0` / `9` | Color cross-talk | physics |
| Numpad `4/1`, `5/2`, `6/3`, `8/7`, `0/9` | Thermal amp/scale/speed/rise/swirl | thermal (PgDn) |

### Injection / patterns

| Key | Pattern |
|---|---|
| `1` | H-bars |
| `2` | V-bars |
| `3` | Centre dot |
| `4` | Checker |
| `5` | Gradient |
| `6` | Noise field (random RGB per pixel) |
| `7` | Concentric rings |
| `8` | Spiral |
| `9` | Polka dots |
| `0` | Starburst / radial rays |
| `Alt+B` | **Bouncer** — low-res pong box, runs 10 seconds |
| `Space` (hold) | Inject current pattern while held |

The bouncer is the only animated pattern and uses a host-side hold
timer that bypasses the normal inject fade. All other patterns fire
once and fade over ~20 frames.

### Quality cycles (archetypes — not up/down)

| Key | Cycles through |
|---|---|
| `PgUp` | Blur kernel: 5-tap cross → 9-tap Gauss → 25-tap Gauss |
| `F12` | CA sampler: 3-sample → 5-ramp → 8-wavelength |
| `Home` | Noise type: **white → pink 1/f → heavy static → VCR → dropout** |
| `End` | Coupled fields: 1 → 2 → 3 → 4 |
| `Delete` | Pixelate style: **off → dots s/m/l → squares s/m/l → rounded s/m/l** (10 states) |
| `Ctrl+Delete` | Pixelate CRT bleed: **off → soft → CRT → melt → fried → burned** |
| `Alt+Delete` | Reroll "burned" dead-pixel pattern (only audible when bleed = burned) |

**Dropout noise + music** — the most fun pairing in the whole app.
Set noise to `dropout` (cycle `Home` four times), start music, each
drum fires a distinct glitch character.

### Post / output

| Key | Action |
|---|---|
| `Ctrl+↑` / `Ctrl+↓` | Output fade toward white / black (this DOES feed back) |
| `Alt+↑` / `Alt+↓` | Brightness (display-only; doesn't feed back) |
| `Alt+[` / `Alt+]` | VFX slot 1: cycle effect back / forward |
| `Alt+\` | VFX slot 1: off |
| `Alt+;` / `Alt+'` | VFX slot 1: param ± |
| `Alt+/` | VFX slot 1: cycle B-source (camera / self) |
| `Ctrl+Alt+[` etc. | Same chords for VFX slot 2 |

V-4-inspired VFX catalogue: Strobe · Still · Shake · Negative ·
Colorize · Monochrome · Posterize · ColorPass · Mirror H/V/HV ·
Multi H/V/HV · W-LumiKey · B-LumiKey · ChromaKey · PinP.

### BPM / music

| Key | Action |
|---|---|
| `Tab` | Tap tempo (inert when MIDI Clock is live) |
| `Ctrl+Tab` | BPM sync on/off |
| `Alt+Tab` | Beat division: x2 / x1 / ÷2 / ÷4 |
| `Ctrl+Alt+I` | Toggle inject-on-beat |
| `Ctrl+Alt+S` | Toggle strobe-rate lock |
| `Ctrl+Alt+V` | Toggle VFX-1 auto-cycle |
| `Ctrl+Alt+F` | Toggle fade-flash |
| `Ctrl+Alt+D` | Toggle decay-dip |
| `Ctrl+Alt+H` | Toggle **hue jump** (on by default) |
| `Ctrl+Alt+=` / `Ctrl+Alt+-` | Hue-jump step ± (0-100 range, progressive) |
| `Ctrl+Alt+R` | Toggle **beat-driven invert flip** |
| `Ctrl+Alt+,` / `Ctrl+Alt+.` | Invert-flip divisor ± (flip every N beats) |
| `Ctrl+Alt+N` / `Ctrl+Alt+P` | Next / previous music preset |
| `Ctrl+Alt+Space` | Music play / pause |
| `Ctrl+M` | Install MIDI virtual-port driver (Windows, first-run) |

Hue-jump-step values you'll actually want:
- **25** — quarter rotation per beat (full cycle over 4 beats = 1 bar)
- **50** — half rotation = complementary colour every beat
- **12** (default) — eighth rotation, 2-bar cycle, very gentle

### App / session

| Key | Action |
|---|---|
| `H` | Toggle help panel |
| `C` | Clear all fields to black |
| `P` | Pause (couples to music; resume restores music state) |
| `\` | Reload shaders from disk (live edit) |
| `F11` | Fullscreen toggle |
| `` ` `` (backtick) | Start / stop EXR recording |
| `PrtSc` | PNG screenshot at sim res (no HUD) |
| `Ctrl+S` | Save current state as preset (`presets/auto_<ts>.ini`) |
| `Ctrl+N` / `Ctrl+P` | Next / previous preset |
| `?` (`/`) | Print help to stdout |
| `Esc` | Quit — **first Esc arms, second Esc or `Y` confirms, `N` cancels** |

---

## Gamepad (Xbox)

Left stick translates · Right stick: X rotates, Y absolute output fade.
**`Back` (View) button toggles the help panel** — open help to see the
per-section mapping change as you navigate between sections.

| Button | Default (closed-help context) |
|---|---|
| A | Tap tempo |
| B | Clear fields |
| X | Pause |
| Y | Help toggle |
| LB / RB | VFX-1 cycle back / forward |
| D-pad U/D | Next / prev preset |
| D-pad L/R | VFX-2 cycle back / forward |
| Start | Recording toggle |
| Back | Help toggle |
| LS click | Toggle couple layer |
| RS click | Toggle external (camera) layer |

The buttons remap *per-help-section* once the help panel is open —
e.g. in the Warp section the A/B/X/Y buttons drive zoom / theta /
pivot / translate. Open help and the bottom-right tag shows which
section owns the current button layout.

---

## Recording a session

Press **`` ` ``** (backtick) to start. A `recordings/feedback_<timestamp>/`
directory fills with `frame_NNNNNN.exr` half-float RGBA images — one
per rendered frame, lossless from the float sim FBO (NOT the 8-bit
display buffer).

Press **`` ` ``** again to stop.

On exit the app scans for unencoded recordings and offers to convert
each via ffmpeg. Preset 1 (recommended) = HEVC at source res, CQ 22,
NVENC if available else libx265. Preset 6 = "encode every preset for
side-by-side". Preset 7 = keep EXR only, no encode.

For screenshots: **`PrtSc`** saves a PNG at sim resolution into
`screenshots/`, no HUD. Good for thumbnails.

---

## Adding music

The app ships with an embedded QuickJS runtime + a clean-room Strudel-
syntax pattern engine + a native audio output. **No external DAW
needed.**

### 1. Write a pattern

Create a new `.strudel` file in the `music/` directory next to the
exe. Any filename — it'll appear in the preset cycle alphabetically.

Example `music/06_mything.strudel`:

```js
// Four-on-the-floor kick + offset hats, with a bassline whose filter
// cutoff sweeps with the visual theta rotation.
stack(
  s("bd bd bd bd").gain(0.9),
  s("~ hh ~ hh ~ hh ~ hh").gain(0.35),
  s("~ ~ cp ~").room(0.2),
  note("c2 ~ eb2 ~ g2 ~ c2 ~")
    .s("saw")
    .lpf(400 + fb.theta * 2500)
    .gain(0.3)
    .attack(0.002).release(0.08)
)
```

Key points:

- `stack(...)` plays its arguments simultaneously. `cat(...)` plays
  them sequentially, one per cycle.
- `s("bd sn hh")` triggers **samples** by name. The built-in short
  names are: `bd` kick, `sn`/`sd` snare, `hh`/`ch` closed hat,
  `oh` open hat, `cp` clap, `rim`, `cb` cowbell.
- `note("c4 e4 g4")` triggers **synth** notes at pitch. Use `c4`
  style letter names or raw MIDI numbers (`60`, `64`, `67`).
- Effects chain: `.gain(0.8)`, `.pan(0.3)`, `.lpf(1200)`, `.hpf(80)`,
  `.room(0.2)` reverb send, `.delay(0.3)` delay send, `.attack(0.01)`,
  `.release(0.1)`, `.s("saw")` to pick the synth waveform (`sine`,
  `saw`, `square`, `tri`).
- **Live video scalars** — any `fb.<name>` reference in your pattern
  reads the live visual state:
  - `fb.zoom` (warp zoom)
  - `fb.theta` (warp rotation)
  - `fb.hueRate`, `fb.decay`, `fb.contrast`, `fb.chroma`, `fb.blur`,
    `fb.noise`, `fb.inject`, `fb.outFade`, `fb.paused`, `fb.beatPhase`
  - So `.lpf(400 + fb.theta * 2500)` sweeps filter cutoff with your
    rotation knob.

### 2. Hot reload

Save the file while the app is running. The music engine stats
the active preset every ~250 ms and re-reads on mtime change. Edit
live.

### 3. Cycle presets live

- **`Ctrl+Alt+N`** / **`Ctrl+Alt+P`** — next / previous preset
- **`Ctrl+Alt+Space`** — play / pause
- **`Space` (hold)** — live gesture: jumps to the `01_breakbeat`
  preset while held, returns on release. Also fires the visual
  inject simultaneously.

### 4. Custom samples

Drop `.wav` files into a `samples/` directory next to the exe. They
replace the synthesised fallback drums.

Naming: `bd.wav`, `sn.wav`, `hh.wav`, `oh.wav`, `cp.wav`, `rim.wav`,
`cb.wav` swap out the built-ins. Any other filename is available as
a new sample in `s("yourname")`.

48 kHz stereo float WAV is ideal; the loader accepts whatever
miniaudio can decode (WAV/FLAC/MP3 to start).

### 5. External Strudel via MIDI

You can drive the app from Strudel (the web IDE) over MIDI Clock +
Note-On messages. feedback.exe registers itself as a virtual MIDI
**input** port named `feedback` (Windows; via the teVirtualMIDI
driver). First run: press **`Ctrl+M`** to auto-install the driver
through winget, or take picker option #7.

In Strudel:

```js
// Drive tempo
midicmd("clock*24,<start stop>/2").midi("feedback")

// Send pattern events that map to actions
s("bd sn").midi("feedback")

// Send CCs that map to binding-table CC entries (see bindings.ini)
cc(1, sine.range(0, 127)).midi("feedback")
```

MIDI Clock drives BPM when live — the BPM section flips to
`LOCKED` and your Tab taps are inert until the clock stops.

### 6. Music + visual closed loop

Listen to the breakbeat preset (`01_breakbeat`) while you turn `W`
(theta) — the bassline filter sweeps with the rotation. That's the
`fb.theta` scalar in action.

Set noise to `dropout` (`Home` four times), start any preset, and
each drum fires its own visual glitch flavour (kick = wide black,
snare = white flash, hat = green speckle, bass = hue-rotating
blocks, other = rainbow).

That's the music → visuals direction. The reverse direction (visual
state → music-modulation) is `fb.*` in your `.strudel` patterns.
Together they close the loop.

---

## When things go wrong

- **Black screen on start** — normally boot-inject seeds the field;
  if the window stays black for more than a second, press `3` then
  tap `Space` to manually inject.
- **Field saturated to solid colour** — press `C` to clear, then
  reduce contrast (`Y`) or turn on decay (`F6` if off) and bump it
  down (`J` multiple times).
- **NaN / broken field (colours go weird after heavy contrast)** —
  already auto-sanitised at the output stage; should self-heal
  within a frame. If it doesn't, press `C`.
- **Music plays but no beat sync** — you need `Ctrl+Tab` on AND at
  least a few `Tab` taps (or an external MIDI Clock source).
- **Nothing happens when I press a parameter key** — the HUD will
  post "*X set — layer off, <F-key> to activate*". Press the named
  F-key to turn the layer on.
- **Crash on launch** — check stdout for the last line printed;
  most crashes surface before the window opens. Common causes:
  driver too old for GL 4.6 core, missing DLLs (shouldn't happen
  with the release zip — statically linked).
- **Camera won't work (external layer)** — feedback.exe needs
  NV12, YUY2, or RGB24 formats. Some cameras only offer MJPG which
  needs Media Foundation's video processor; not wired up yet. The
  `external` toggle silently does nothing if no camera is negotiated.

---

## File layout at runtime

```
feedback.exe             # the binary
shaders/                 # layer .glsl files (editable, `\` reloads)
presets/                 # .ini files (01-05 curated, auto_* user-saved)
music/                   # .strudel files (edit live, hot-reloaded)
samples/                 # optional WAV overrides for drum names
recordings/              # EXR sequences + generated MP4s
screenshots/             # PNG stills from PrtSc
bindings.ini             # key/pad/MIDI mapping overrides
```

You can move the exe anywhere — it looks for `shaders/`, `presets/`
etc. either in the CWD or next to the exe.
