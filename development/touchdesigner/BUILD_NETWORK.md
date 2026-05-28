# Build a TD network for Crutchfield OSC control

`.toe` files are binary and we'd rather not ship one that ages out
relative to your TD version, so here's the recipe. 5 minutes start to
finish.

## Minimum viable network

```
[ Slider COMP / panel of UI sliders ]
              │
              ▼
       [ Math CHOP ] ← optional smoothing / range remap
              │
              ▼
       [ OSC Out CHOP ]    ──→  127.0.0.1 : 7700
       (channel names match
        /cma/* addresses)
```

## Step by step

1. **Drop an OSC Out CHOP** anywhere in your network.
2. Set its parameters:
   - `Network Address` = `127.0.0.1`
   - `Network Port`    = `7700`
   - `Send Mode`       = `Send Channel Values`
   - `Send Every Frame` = `On`
3. **Name the upstream CHOP's channels** to match the addresses you
   want to drive. Use `/` separators in TD channel names; they'll
   become slashes in the OSC address.

   Examples:
   | Channel name      | OSC address sent |
   | ----------------- | ---------------- |
   | `cma/decay`       | `/cma/decay`     |
   | `cma/layer/noise` | `/cma/layer/noise` |
   | `cma/shape/count` | `/cma/shape/count` |

   You can also use a `Rename CHOP` upstream to remap any channel set.
4. **Verify reception.** Launch Crutchfield with `--osc-listen
   --osc-learn` and twiddle any control in TD. You should see
   `[osc-learn] /cma/<channel> f=<value>` print on the Crutchfield
   console.
5. **Wire bindings.** Append
   `bindings.examples/crutchfield_touchdesigner.ini` to your
   Crutchfield `bindings.ini`. Restart Crutchfield. Every channel in
   the JSON map is now live.

## Triggers (one-shot buttons)

Buttons / triggers need momentary semantics. Two approaches:

**A) Pulse on press only (recommended).** In TD use a Button COMP →
`Trigger CHOP` (set to one-shot) → OSC Out CHOP. The trigger emits
1.0 for one frame, 0.0 the rest. Crutchfield fires on the rising edge.

**B) Send 1.0 explicitly.** A Constant CHOP with value 1 routed through
a Pulse Trigger fires the action once per pulse. Use `osct:` in your
binding (`app.screenshot = osct:/cma/shot`) to be explicit.

## Smoothing / shaping

OSC values are dispatched as-is. If your TD source is jittery (e.g.
audio analysis, MIDI 7-bit CC), insert a `Filter CHOP` upstream:

| Filter type | Use for |
| ----------- | ------- |
| Lag         | gentle exponential smoothing for audio reactives |
| Slope Limit | hard-cap rate-of-change for jumpy MIDI 7-bit |
| Spring      | musical bounce on triggers |
| Resample    | up-rate slow sources to match TD's frame rate |

## Bidirectional? (future)

Phase 3 of the OSC plan adds optional outgoing OSC so a TD UI can
mirror Crutchfield's current values. Not in v1.

## Window picker

Crutchfield's UI panel (the on-screen overlay) responds to the same
actions. Anything you drive over OSC also updates the on-screen state.
