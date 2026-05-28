# Bindings reference

Every OSC binding syntax, every flag, and exactly how each one transforms the wire value before dispatch.

## Where bindings live

`~/Library/Application Support/Crutchfield Machine/bindings.ini` on macOS (the directory is auto-created on first run; the file is rewritten with the current effective bindings on every clean exit, so you can edit a section, restart, and the file persists with your edits intact).

The `[osc]` section is the new one. The other sections (`[keyboard]`, `[gamepad]`, `[midi]`) are unchanged.

## Section header keys

```ini
[osc]
listen = 7700         # UDP port to bind. 0 = disabled.
learn  = on           # print every incoming OSC msg to stdout (off|on)
```

CLI flags (`--osc-listen [PORT]`, `--osc-learn`) override these if set; otherwise the INI values win.

## Binding syntax

```
<action.name> = <spec> [flag]...
```

Two specs are valid in the `[osc]` section:

```ini
# Auto-pick: SRC_OSC_F if action is continuous (AK_STEP/RATE),
#            SRC_OSC_TRIG if action is discrete/trigger
action.name = osc:/path/to/address [flags]

# Force trigger semantics regardless of action kind
action.name = osct:/path/to/address [flags]
```

`osc:` is what you want 99% of the time. Use `osct:` only when you have a continuous-shaped TD source (e.g. a Pulse Trigger CHOP that sends 1.0 then 0.0) wired to an action that's continuous but you want momentary press/release behavior.

### Address rules

- Must start with `/`
- Up to 127 characters (truncated if longer)
- Matched literally by `strcmp` — no wildcards
- Case-sensitive
- Slashes form a hierarchy by convention (`/cma/layer/noise`) but are just chars to us

## Flags

| Flag | Effect | Applies to |
| --- | --- | --- |
| `scale=X` | Multiply value by X before dispatch | both `osc:` and `osct:` |
| `invert` | Negate value before dispatch (after scale) | both |
| `bipolar` | Remap 0..1 → −1..+1 (right for centered controls) | `osc:` only — meaningless on triggers |
| `delta` | Dispatch *change since last value* instead of raw value | `osc:` only |

### `scale=X` — multiplier

Pre-dispatch multiply. `mg = arg * scale`.

```ini
# 0..1 incoming, but the action expects 0..2 (sat range goes from greyscale to oversaturated)
color.sat.setAxis = osc:/cma/sat scale=2.0

# 0..1 incoming, attenuated to a gentler 0..0.5 range
dyn.decay.axis = osc:/cma/decay scale=0.5
```

Default `1.0`. Combine with `invert` for negative scaling.

### `invert` — negate

`mg = -mg` after scaling. Useful when your source's polarity is wrong.

```ini
# Slider sends 0=top 1=bottom, but you want top=full
dyn.decay.axis = osc:/cma/decay invert scale=1.0
```

### `bipolar` — center at zero

Remaps 0..1 → −1..+1. Right for controls that should center around a neutral 0:

```ini
# outfade: -1 = full black, 0 = no fade, +1 = full white
outfade.axis = osc:/cma/outfade bipolar

# zoom: -1 = max in, 0 = neutral, +1 = max out (with delta semantics in the action)
warp.zoom.axis = osc:/cma/zoom bipolar
```

If your TD source already sends −1..+1 directly, **don't** use `bipolar` — that would double-remap to 0..1.

### `delta` — change since last value

Dispatches the *difference* between this value and the previous one for the same address.

```ini
# Encoder-style knob: knob value rises 0.5 → 0.7,
# we dispatch +0.2 instead of 0.7
warp.zoom.axis = osc:/cma/zoom delta scale=2.0
```

First message for an address establishes the "previous" value and dispatches nothing. Subsequent messages dispatch the delta.

Important caveats:
- Per-address state lives in an `unordered_map<string, float>` in OSC runtime. Cleared when the OSC port changes.
- Doesn't reset on layer changes / preset loads.
- If the source sends absolute positions but you want them as nudges, this is the flag.

## Source-type auto-selection

The parser picks `SRC_OSC_F` or `SRC_OSC_TRIG` from the action's `kind`:

| Action kind | Auto-pick | Why |
| --- | --- | --- |
| `AK_STEP` | `SRC_OSC_F` | continuous nudge (`+`/`-` actions, axis setpoints) |
| `AK_RATE` | `SRC_OSC_F` | continuous integration |
| `AK_DISCRETE` | `SRC_OSC_TRIG` | one-shot fire on press (pattern selects, layer toggles) |
| `AK_TRIGGER` | `SRC_OSC_TRIG` | press + release edges (hold-inject, push-to-talk) |

Use `--list-actions` to see each action's kind by inference (description hints).

`osct:` overrides this and forces `SRC_OSC_TRIG`. There's no `oscf:` to force `SRC_OSC_F` — if you really need to drive a discrete action with continuous semantics, ask and we'll add it.

## Dispatch semantics

After flag transformations the binding fires through `handler_(action, mg)` and the handler interprets `mg` based on the action's kind.

### `SRC_OSC_F` dispatch

```cpp
norm = arg_f                            // first arg as float
if (delta) {
    mg = norm - prev_value[address]
    prev_value[address] = norm
}
else if (bipolar) mg = norm * 2 - 1
else              mg = norm
if (invert) mg = -mg
mg *= scale

switch (action.kind) {
case AK_RATE:
case AK_STEP:
    fire(action, mg)
case AK_DISCRETE:
    if (norm > 0.5) fire(action, 1.0)
case AK_TRIGGER:
    fire(action, norm > 0.5 ? 1.0 : 0.0)
}
```

### `SRC_OSC_TRIG` dispatch

```cpp
norm = (no args) ? 1.0 : arg_f

switch (action.kind) {
case AK_DISCRETE:
    if (norm > 0.5) fire(action, 1.0)         // single press
case AK_TRIGGER:
    fire(action, norm > 0.5 ? 1.0 : 0.0)      // both edges
case AK_STEP:
case AK_RATE:
    // continuous action bound as trigger — fire scaled
    mg = norm * scale
    if (invert) mg = -mg
    fire(action, mg)
}
```

The `> 0.5` threshold is the same one MIDI CC uses for its discrete dispatch (CC value > 63 means "on"). Consistent across input sources.

## Worked binding examples

### Knob → continuous parameter

```ini
dyn.decay.axis = osc:/cma/decay
```

TD slider sends 0..1; decay sweeps from 0 (instant decay, no feedback) to 1 (infinite memory).

### Knob → centered (bipolar) parameter

```ini
outfade.axis = osc:/cma/outfade bipolar
```

TD slider 0..1; 0.5 is neutral (no fade), 0 is full black, 1 is full white.

### Pad → layer toggle

```ini
layer.noise = osc:/cma/layer/noise
```

Any value > 0.5 toggles the noise layer. Sending the same address again toggles back. Sending 0 does nothing (it's a discrete action; release isn't meaningful).

### Pulse → screenshot

```ini
app.screenshot = osc:/cma/shot
```

Or explicitly force trigger semantics:

```ini
app.screenshot = osct:/cma/shot
```

Either way, a 1.0 fires the screenshot.

### Hold pattern (press + release)

```ini
inject.hold = osc:/cma/inject/hold
```

Inject pattern fires while value > 0.5, releases when value ≤ 0.5. Pair with a TD Constant CHOP gated by a Button COMP — Button on = inject, Button off = release.

### Encoder nudge

```ini
warp.zoom.axis = osc:/cma/zoom/encoder delta scale=2.0
```

Knob 0..1 sends absolute position. Each change dispatches `2 × (this − last)`, i.e. moving the knob from 0.5 to 0.55 nudges zoom by +0.1.

### Inverted slider

```ini
out.fade.up = osc:/cma/fade invert
```

Hardware sends 0=top, 1=bottom, but you want top = "fade up". `invert` flips the sign so top = +1.

### Scaled bipolar (rotation in radians-ish)

```ini
warp.theta.axis = osc:/cma/theta bipolar scale=3.14159
```

0..1 in → −π..+π out.

## Dedup behavior

When `bindings.ini` is loaded, each parsed binding **replaces** any prior binding for the same `(action, source, context, oscAddress)` tuple. Concretely:

- Two `[osc]` lines with the same address but different actions → both kept (one address can drive multiple actions)
- Two `[osc]` lines with the same action AND same address → second replaces first
- An `[osc]` binding does NOT remove the matching MIDI binding for that action — both stay live

This means you can append the LC v1 ini AND the TouchDesigner ini to the same `bindings.ini` and use both controllers simultaneously without conflict.

## Persistence

On clean exit, the running app writes the **effective** binding set back to `bindings.ini` (including any defaults). For the `[osc]` section, this writes `listen=N` and `learn=on|off` from the current runtime state. If you launched with `--osc-listen --osc-learn`, those settings persist into the file so next launch (without flags) still has OSC enabled.

If you want OSC enabled without specifying it on every launch, edit `bindings.ini` directly:

```ini
[osc]
listen = 7700
learn  = off
```

…or just launch once with the flags and let the auto-save take care of it.
