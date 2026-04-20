# ADR-0009 — Gamepad controls rebind per help section; menu navigation is the only reserved surface

**Status:** Accepted
**Date:** 2026-04-19

## Context

With the action registry (ADR-0007) in place and an Xbox controller
hooked up, the first default gamepad map was "global": LS = translate,
RS = rotate/fade, A = tap-tempo, B = clear, etc. Every control mapped
to one action, everywhere.

The feedback app exposes ~80 tunable parameters across 14+ functional
groups (warp, optics, color, dynamics, physics, thermal, two VFX
slots, BPM, output, …). A single global map burned the controller
surface on a handful of "live performance favorites" and left most
parameters unreachable from the pad. The user explicitly pushed back:

> I am reluctant to reserve so much control surface for global
> things... I prefer to have the whole controller go over to that
> now selected section.

The help panel already groups parameters by section (ADR-nil, see
`HELP_SECTIONS[]` in `main.cpp`). The pad could follow the panel's
focus.

## Decision

Gamepad bindings are **contextual by section**. The `Binding` struct
gains a `BindContext` field (`CTX_ANY`, `CTX_MENU`, or
`CTX_SEC_<section>`). `pollGamepad` filters each frame to
`(context == CTX_ANY || context == current)`.

**Current context** is determined by the overlay state:

- Help open, menu view → `CTX_MENU`. Only navigation bindings fire.
- Help open, section view → `CTX_SEC_<N>` where N is the drilled-in
  section.
- Help closed → `CTX_SEC_<N>` where N is the last-drilled section
  (tracked in `Overlay::activeSection_`, defaults to `Warp`).

**Reserved-always** — the only gamepad binding that lives in
`CTX_ANY`:
- Back (View) button = `ACT_HELP` toggle.

**Reserved in menu view** — D-pad U/D navigate, A enter, B close.

**Reserved in section view** — D-pad U/D scroll the body, B returns
to menu.

**Free in section view + help closed** — everything else: both
sticks, LB/RB, X/Y, LT/RT, Start, LS-click, RS-click, and (when help
is closed) also D-pad and A.

Per-section default maps fill that surface generously. Examples:

- **Color**: LS-X hue, LS-Y sat, RS-X contrast, RS-Y gamma, LT/RT
  hue slew at higher rate.
- **Thermal**: LS-X scale, LS-Y amp, RS-X speed, RS-Y rise, LT/RT
  swirl −/+.
- **Layers**: D-pad L/R move an armed cursor through 12 layers;
  A toggles armed. Plus direct shortcuts (X warp, Y optics, LB/RB
  external/inject) for live pulls.
- **VFX-1/2**: LB/RB and D-pad L/R cycle effect, LT/RT param, X off,
  Y B-source.

**Help panel renders a legend** under each section's body, showing
the live map. Discoverable without reading docs.

**Bottom-right tag** when help is closed: `<ActiveSection> · Back:
help`. Always visible when a gamepad is connected; updates as the
user drills into different sections.

## Consequences

**Positive:**
- Each section gets the whole controller. Color has 4 axes of pad
  surface dedicated to 4 color params — direct and responsive.
- No mode modals — the help panel IS the mode selector. Nothing to
  memorize; drill in, and the pad now drives those params.
- Extending is cheap: a new section defines its own map plus legend;
  the dispatcher doesn't change.
- The user can switch what the pad does on the fly by navigating
  the menu, which is already natural.

**Negative:**
- Tap-tempo used to be one button press; now it requires drilling
  into the BPM section first (or using the keyboard). Compensated
  by making Strudel-driven MIDI clock the primary tempo source
  (planned).
- Recording start/stop moved off "anywhere" onto the App section
  (and the backtick keyboard chord). Same tradeoff.
- Discovery burden: new users must learn that menu navigation
  drives the pad's current role. The bottom-right tag and the
  startup print mitigate this.
- `BindContext` must stay aligned with `HELP_SECTIONS[]` — we rely
  on `CTX_SEC_STATUS + N == section index N`. A reordering breaks
  dispatch silently. Mitigation: a comment at the enum declaration
  notes the invariant.

## Alternatives considered

- **Keep the global gamepad map; add per-section extras.** Compromise
  that keeps tap-tempo always-on at the cost of leaving most
  parameters unreachable from the pad. User explicitly rejected.
- **Two stick modes, switched by a modifier button.** Similar reach
  but harder to build up muscle memory.
- **Dedicated "pad modes" independent of the help panel.** Would
  double up UI surface — two mode switchers for the same thing.
- **Learn-mode UI** that lets the user assign any pad button to any
  action at runtime. Powerful but heavyweight; would need an on-pad
  UI. Deferred; the INI-level rebinding covers the want.

## References

- `input.h::BindContext`, `Binding::context` — the enum and field.
- `input.cpp::Input::pollGamepad` — context filter.
- `main.cpp` — `help_section_body`, `legend_for_section`,
  `section_*` builders. Section-specific map definitions live in
  `input.cpp::installDefaults`.
- `overlay.cpp::drawHelpSection` — renders the legend.
- ADR-0007 — the action registry this depends on.
