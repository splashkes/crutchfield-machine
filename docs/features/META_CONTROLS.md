# Math-derived meta-controls

The Mathlab analytical layer isn't just for looking at — it's the substrate for a new class of controls. Instead of "turn the decay knob to some number," meta-controls let you say "set the memory to 2 seconds" or "walk us into CHAOTIC regime" and the math derives the parameter values.

Five meta-controls ship in this release:

| Action | What it controls | Math |
| --- | --- | --- |
| `dyn.halflife.axis` | memory half-life in **seconds** (log mapped 0.05..10) | `decay = 2^(−1/(h·60))` |
| `dyn.halflife.beats.axis` | half-life in **beats** at Link tempo | as above, after `h_sec = beats × 60 / bpm` |
| `regime.distance.axis` | one fader: STABLE → CHAOTIC walk | piecewise (K_c, decay, noise) path |
| `regime.set` | discrete regime jump (0..3) | 70/30 blend toward target tuple |
| `regime.invert` | cross nearest regime boundary | ±5% of K_c around 0.3 or 0.6 |
| `pad.regime.x` + `.y` | radial compass (XY → regime quadrant) | polar → 4-way LUT blend |
| `theater.failsafe` | DIVERGENT >2s → auto-recall STABLE | regime watcher + snapshot tag |
| `math.echo` | publish `/cma/math/*` outbound at 30 Hz | classify + send |

## 1 — `dyn.halflife.axis`

**Pitch**: a knob measured in seconds, not in raw decay coefficient.

Decay is a per-frame multiplier in [0..1]. Saying "I want a 2-second tail" requires solving `decay = 2^(−1/(2·60))` = 0.99423… by hand. This action does that math.

```ini
[osc]
dyn.halflife.axis = osc:/cma/dyn/halflife
```

Send a value in [0..1]; it log-maps to [0.05..10] seconds. So `0.5` → ~0.7s, `0.7` → ~2.3s, `1.0` → 10s.

```bash
python3 development/osc_send.py /cma/dyn/halflife 0.5
# HUD: "half-life → 0.71 s  (decay 0.9837)"
```

### Audio-locked variant

```ini
[osc]
dyn.halflife.beats.axis = osc:/cma/dyn/halflife/beats
```

Same idea, but the units are beats at the current Link tempo. `0.5` → ~1 beat half-life. Memory locks to the music.

## 2 — `regime.distance.axis`

**Pitch**: one fader walks you from deep STABLE to deep CHAOTIC.

Internally, the action knows where the regime thresholds sit in parameter space. A linear move in the input fader produces a piecewise path through (K_c, decay, noise) that:

- t=0.0 → STABLE deep (K_c=0.05, decay=0.97)
- t=0.5 → TURBULENT entry (K_c=0.35)
- t=1.0 → CHAOTIC entry (K_c=0.70, decay=0.995)

```ini
[osc]
regime.distance.axis = osc:/cma/regime/distance
```

```bash
python3 development/osc_send.py /cma/regime/distance 0.7
# system enters CHAOTIC; Mathlab REGIME badge turns red
```

### Default Launch Control binding

Bind to the big slider on an LC XL:

```ini
[midi]
regime.distance.axis = cc:7 ch=9
```

Slider all the way down = deep stable. All the way up = full chaos. One control to perform a 3-minute dynamic arc.

## 3 — `pad.regime.x` + `pad.regime.y`

**Pitch**: a 2D pad where angle picks the regime and radius picks intensity.

Center of pad = neutral (deep STABLE). Drag clockwise from East → North → West → South to walk through the four cardinal regimes (STABLE / TURBULENT / CHAOTIC / MARGINAL). The further from center, the deeper into that regime.

Maps perfectly to a TouchOSC XY pad or two Launch Control knobs.

```ini
[osc]
pad.regime.x = osc:/cma/pad/regime/x
pad.regime.y = osc:/cma/pad/regime/y
```

In TouchOSC: add an XY pad. In TouchDesigner: an `OSC Out CHOP` with two channels.

```bash
# Northeast — between STABLE and TURBULENT
python3 development/osc_send.py /cma/pad/regime/x 0.85
python3 development/osc_send.py /cma/pad/regime/y 0.85
```

## 4 — `theater.failsafe`

**Pitch**: hands-off insurance against blowups during live shows.

When armed and the regime stays DIVERGENT (ρ > 1.001) for more than 2 seconds, Crutchfield automatically recalls the most-recent snapshot tagged as STABLE (snapshots auto-tag with their regime at save time). If there isn't one, it falls back to ACT_CLEAR. A 3-second cooloff prevents recovery-flicker loops.

```ini
[osc]
theater.failsafe = osc:/cma/theater/failsafe
```

```bash
# Arm before going on stage
python3 development/osc_send.py /cma/theater/failsafe 1
# HUD: "theater.failsafe ARMED  (DIVERGENT >2s → recall STABLE)"
```

While armed, push decay+couple aggressively without anxiety — if the system blows out, it self-heals within 2 seconds. The cooloff means if you keep pushing into divergent, it'll keep recovering, not flicker. Disarm with the same toggle.

### Setup

Pre-show prep: at sound check, when the system is in a known-good STABLE state, hit `/cma/snapshot/save 1`. The snapshot's regime is auto-tagged STABLE. That's the recovery target. As you keep snapshotting throughout the night (different STABLE configs for different songs), the failsafe always picks the most recent one.

## 5 — `math.echo`

**Pitch**: Crutchfield's dynamical state becomes the show stack's API.

When enabled, the math model publishes at 30 Hz on these addresses:

```
/cma/math/rho                  float  spectral radius
/cma/math/halflife             float  seconds
/cma/math/diffusion            float
/cma/math/coupling             float
/cma/math/noise/db             float
/cma/math/regime               int    0=STABLE 1=TURBULENT 2=CHAOTIC 3=MARGINAL 4=DIVERGENT
/cma/math/regime/changed       int    edge-triggered on transition only
```

Subscribe from anywhere — TouchDesigner, Resolume, lighting console, Vezér.

```ini
[osc]
math.echo = osc:/cma/math/echo
```

```bash
python3 development/osc_send.py /cma/math/echo 1
# HUD: "math.echo ON  (/cma/math/* at 30 Hz)"
```

### Use case: lighting that follows the visual regime

The LD subscribes their console to `/cma/math/regime`. They map:
- 0 (STABLE) → warm amber wash, no movement
- 1 (TURBULENT) → cool blues, slow pulse
- 2 (CHAOTIC) → full strobes
- 3 (MARGINAL) → red wash, holding
- 4 (DIVERGENT) → blackout (the visual blew out anyway)

Sean drives `/cma/regime/distance` from his XY pad. The visual changes. The lights follow automatically. No cue-by-cue programming — the regime classifier is the single source of truth.

### Edge events

`/cma/math/regime/changed` fires only on transition. Bind a button on a sampler to it: every time the visual crosses into CHAOTIC, the sampler triggers a one-shot. Cross-modal trigger from the visual dynamics into the music.

## Composition tip — meta-controls compose

Bind:
- LC fader 1 → `regime.distance.axis`
- LC knob 1 → `dyn.halflife.axis`
- TouchOSC XY pad → `pad.regime.x` + `pad.regime.y`
- Launch Control button → `theater.failsafe`
- TouchDesigner OSC out → `math.echo` config

You're now playing the **dynamics** as instruments. The actual parameters (decay, couple, noise) become implementation details that the math derives. That's the instrument evolution this release is about.

## Implementation pointers

- `input.h` — `ACT_DYN_HALFLIFE_AXIS`, `ACT_REGIME_DISTANCE_AXIS`, `ACT_REGIME_SET`, `ACT_REGIME_INVERT`, `ACT_PAD_REGIME_X/Y`, `ACT_THEATER_FAILSAFE`, `ACT_MATH_ECHO_TOGGLE`
- `input.cpp` ACTIONS table — registered under `[Math]` and `[Dynamics]` groups
- `main.cpp apply_action` — handler per action computes parameters from the math
- `main.cpp classify_regime`, `failsafe_tick`, `math_echo_tick` — file-scope helpers, ticked once per frame after `mathPushFrame`
- `g_failsafe_enabled`, `g_math_echo_enabled` — file-scope globals flipped by the toggle actions
- Snapshot's `regimeCode` field — tagged at save, queried by `snapshot_last_with_regime` for failsafe recovery
