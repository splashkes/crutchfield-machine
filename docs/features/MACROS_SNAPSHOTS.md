# Macros + State snapshots — scene recall

Two complementary scene-recall features.

- **Macros**: a named sequence of action+value steps fired as one logical unit. Defined in `bindings.ini`'s `[macros]` section.
- **Snapshots**: 8 numbered slots holding a complete parameter state. Save/recall by slot number.

## Macros

### Defining

```ini
[macros]
scene.calm     = dyn.decay.axis(0.97) ; dyn.noise.axis(0.001) ; color.sat.setAxis(0.4)  ; layer.noise(1)
scene.chaos    = dyn.decay.axis(0.55) ; dyn.noise.axis(0.04)  ; color.sat.setAxis(0.95) ; pattern.spiral
scene.collapse = outfade.axis(-1.0)   ; dyn.decay.axis(0.0)   ; layer.couple(0)         ; app.clear
intro.4count   = pattern.dot ; layer.warp(0) ; layer.optics(1)
```

Format:

```
<macro.name> = <action(value)> ; <action(value)> ; ...
```

Each step is `action.name(value)`. The `(value)` is optional and defaults to `1.0`. Whitespace and trailing semicolons are ignored. Unknown action names emit a warning to stderr and the step is skipped — the rest of the macro still loads.

### Binding to inputs

In any binding section (`[keyboard]`, `[gamepad]`, `[midi]`, `[osc]`), use `macro.<name>` on the LHS:

```ini
[osc]
macro.scene.calm     = osc:/cma/scene/calm
macro.scene.chaos    = osc:/cma/scene/chaos
macro.scene.collapse = osc:/cma/scene/end

[midi]
macro.scene.calm     = note:60 ch=1
macro.scene.chaos    = note:62 ch=1
macro.intro.4count   = note:64 ch=1

[keyboard]
macro.scene.calm     = F1
macro.scene.chaos    = F2
```

A single key/note/OSC message fires the entire sequence atomically.

### How it works under the hood

Macros get **synthetic ActionIds** starting at `ACT_MACRO_BASE` (10000). Each macro registers a unique id. The catalogue (`action_info()`, `action_info_by_name()`) resolves these ids and names transparently, so the existing binding system treats them like any other action. `apply_action()` in `main.cpp` detects ids >= `ACT_MACRO_BASE` and short-circuits to `Input::fireMacroById()`, which iterates the step list and re-enters `handler_` for each step. Each step runs through the full dispatch chain including OSC echo, so a macro firing emits N echo messages — one per step.

Recursive macros work: a macro can include another macro as a step. (Don't loop them.)

### Performance

Macros fire synchronously on the main thread. A 10-step macro adds ~10 µs to the frame it triggers in — imperceptible.

### Listing macros

`./feedback --list-actions` includes registered macros under the `[Macros]` group:

```
scene.calm       [Macros]  macro: scene.calm
scene.chaos      [Macros]  macro: scene.chaos
scene.collapse   [Macros]  macro: scene.collapse
```

The synthetic ids are stable across reloads of the same name; their position in the list reflects the order they were defined.

## State snapshots

### Saving and recalling

Two built-in actions:

| Action | Kind | Magnitude |
| --- | --- | --- |
| `snapshot.save` | step | slot number (1–8) |
| `snapshot.recall` | step | slot number (1–8) |

The dispatched magnitude carries the slot number. Fractional values are floored.

```ini
[osc]
snapshot.save   = osc:/cma/snap/save     ; send int 1..8 to pick slot
snapshot.recall = osc:/cma/snap/recall

[midi]
snapshot.save   = cc:20 ch=9 scale=7.94  ; scale 0..127 to 1..8 ish
```

### What's captured

Every continuous parameter in `S.p` plus the layer `enable` mask:

```cpp
struct StateSnapshot {
    bool   used;
    int    enableBits;
    Params p;          // ~60 floats covering every continuous param
    double savedAt;    // wall-clock seconds at save time
};
StateSnapshot g_snapshots[9];   // indices 1..8
```

Not captured:
- FBO texture contents (the feedback "memory")
- Recording state
- OSC port / listener state
- Audio engine state

So a recall reverts every parameter knob and which layers are on. The actual feedback image continues evolving from wherever it was — it doesn't roll back in time.

### Combining with macros

```ini
[macros]
save.scene.1   = snapshot.save(1)
save.scene.2   = snapshot.save(2)
recall.scene.1 = snapshot.recall(1)
recall.scene.2 = snapshot.recall(2)

[osc]
macro.save.scene.1   = osct:/cma/save/1
macro.recall.scene.1 = osct:/cma/recall/1
```

Note: `osct:` (forced trigger) is recommended for save/recall so they don't fire continuously while value > 0.5.

### HUD feedback

Save / recall log to the on-screen HUD via `S.ov.logEvent()`:

```
snapshot saved → slot 3
snapshot recalled → slot 3
snapshot slot 5 empty
```

Recalling an empty slot is safe — logs the warning, does nothing.

## When to use which

- **Macros** — for parametric changes you know in advance (intro / drop / outro / clear). Hand-authored. Predictable.
- **Snapshots** — for "I like this state right now, save it" mid-show capture. Discovered live. Numeric slot only, no semantic name.

Hybrid pattern: build a 5-macro intro that ends with `snapshot.save(1)`, then later `snapshot.recall(1)` returns to that captured state for an outro callback.

## Implementation pointers

- `input.h`: `MacroStep`, `registerMacro()`, `fireMacro()`, `fireMacroById()`, `macroInfoByIndex()`
- `input.cpp`: `[macros]` parser splits on `;`, `registerMacro` assigns synthetic IDs; `action_info()` / `action_info_by_name()` resolve them
- `main.cpp`: `apply_action()` detects `id >= ACT_MACRO_BASE` → `fireMacroById()`; `ACT_SNAPSHOT_SAVE` / `ACT_SNAPSHOT_RECALL` handlers
- Snapshot storage: `g_snapshots[9]` in `main.cpp`'s anonymous namespace
