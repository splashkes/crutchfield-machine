# ADR-0007 — Action registry + `bindings.ini` as the unified input layer

**Status:** Accepted
**Date:** 2026-04-19

## Context

The original `key_cb` in `main.cpp` grew to ~300 lines of `switch (key)`
cases, each inlining both the policy ("which parameter do we mutate")
and the mechanic ("by how much, with what clamp, post what HUD
message"). Adding Xbox gamepad support meant either duplicating that
whole switch for joystick input or refactoring. We also wanted an
extension hook for MIDI so that hooking up a knob controller later
wouldn't be another copy-paste of the dispatch table.

Constraints:

- Keyboard behaviour had to stay byte-identical — existing muscle
  memory and README descriptions needed to keep working.
- Shift-as-coarse-multiplier had to survive (20× step with Shift held).
- Users already used to editing `presets/*.ini` would expect
  remapping to live in a similar file, not a built dialog.
- Per-binding options (scale, inversion, axis deadzone, absolute vs
  integrating) had to be expressible for gamepad ergonomics.

## Decision

Introduce `input.{h,cpp}` and route every input source through a
single `apply_action(ActionId, float magnitude)` mutator.

- **`ActionId` enum** — one entry per distinct thing the app can do.
  ~120 entries covering layer toggles, parameter nudges, pattern
  select, recording, preset navigation, V-4 slot cycling, BPM, output
  fade, help navigation. Names are stable (`warp.zoom+`,
  `preset.save`).
- **`Binding` struct** — `{ action, source, code, modmask, scale,
  invert, deadzone, absolute, context }`. Sources: `SRC_KEY`,
  `SRC_GAMEPAD_BTN`, `SRC_GAMEPAD_AXIS`, `SRC_MIDI_CC`.
- **`ActionKind`** — `STEP` (per-press nudge), `RATE` (per-frame
  integrated), `DISCRETE` (fire-once), `TRIGGER` (press/release
  pair). Dispatch semantics follow from the kind.
- **`Input::installDefaults()`** — sets up the factory binding table,
  matching every pre-refactor keyboard assignment byte-for-byte.
- **`bindings.ini`** — written next to the exe on first run. Syntax:
  `action.name = KeySpec [scale=X] [invert] [deadzone=X] [abs]
  [ctx=section]`. File entries override defaults keyed by (action,
  source, context).
- **Dispatch** — `Input::onKey`, `Input::pollGamepad`, `Input::pollMidi`
  each convert raw events into `(ActionId, magnitude)` pairs and call
  the host-provided handler. Host supplies `apply_action` which is
  the single source of truth for state mutation.

Shift is deliberately NOT part of the modmask — it's always the
coarse-step multiplier. `Ctrl+S` and bare `S` are distinct bindings;
`Shift+S` and `S` are the same binding with a bigger magnitude.

## Consequences

**Positive:**
- Adding a new control source (gamepad in ADR-0009, MIDI in the
  pending Strudel work) is "parse the events, call the handler" —
  no new action switch to maintain.
- `bindings.ini` gives users rebinding without recompilation, and
  the default file doubles as complete documentation of every
  binding in a browseable form.
- The help overlay's Bindings section reads the active table directly
  so live rebinds update the help without a code change.
- Decoupling policy (what happens) from source (who triggered it)
  made the contextual-gamepad change (ADR-0009) cheap.

**Negative:**
- `main.cpp` gained a ~300-line `apply_action` switch, which looks
  like we just moved the complexity. We did, but we moved it to
  *one* place instead of N, and the per-parameter mutators are now
  grep-able by action name.
- The `ActionId` enum and the binding table must stay in sync by
  hand. If you add an action but forget a default binding, the
  keyboard won't know about it. Mitigation: the help overlay will
  list it under its group with "(unbound)" visible.
- INI format grew another set of option tokens (`scale=`, `invert`,
  `deadzone=`, `abs`, `ctx=`). Documented in the header comment
  that `saveIni()` writes out.

## Alternatives considered

- **Keep `key_cb` as the switch, add `gamepad_cb` as a parallel
  switch.** Simpler but doesn't scale to MIDI, and re-binding would
  require recompilation.
- **Use a third-party input library** (rtmidi, SDL input, Gainput).
  Would solve gamepad and MIDI but adds a build dependency; we're
  already using GLFW which does both keyboard and gamepad natively,
  and `winmm` for MIDI is already linked because the recorder needs
  it.
- **JSON or YAML config instead of INI.** INI matches `presets/*.ini`
  and the existing parser patterns. JSON/YAML would need a new
  dependency.
- **Runtime-editable binding UI.** Deferred — the INI file is more
  robust and easier to diff/share. A UI can be layered on later if
  wanted.

## References

- `input.h` / `input.cpp` — the registry + dispatcher.
- `main.cpp::apply_action` — single source of truth for state
  mutation in response to actions.
- `bindings.ini` (generated) — live binding table, human-editable.
- ADR-0009 — contextual gamepad mapping, which builds on this.
