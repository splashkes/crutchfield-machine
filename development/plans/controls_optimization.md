# Controls optimization plan

Goal: use the `--log-usage` telemetry to find which physical controls
(keyboard keys, DDJ-FLX2 pads/CCs/buttons, gamepad surfaces) carry
real work, which are dead weight, and which actions deserve a finger
reach they don't currently have. Output is a revised default
`bindings.ini` (and probably a couple of code-level changes to the
master-shift bank in `input.cpp`).

## Status

- Usage logger landed in PR #15 (2026-05-16). Every action fire is
  recorded to `usage_YYYYMMDD_HHMMSS.csv` under `~/Library/Application
  Support/Crutchfield Machine/` on macOS, or next to the exe on
  Windows. Columns: `t_sec, source, code, channel, mods, action,
  magnitude`.
- First captured session (84 s, exploratory): jog wheels carried
  everything (CC 34 ch1/2 = ~14k fires); deck-1 pads 2/3/4
  (star/circle/square obstacles) saw zero use; some keys (T, Insert)
  were hit ~1× as one-offs.

Not enough data yet to make permanent remappings — that's what this
plan is for.

## Data review procedure

### What to capture

Run at least three kinds of session, each ~20–40 minutes, with
`--log-usage` on:

1. **Cold start / exploration** — sit down with a blank state, no
   intent. Captures discovery patterns: what do hands reach for
   first, what surfaces feel unnatural.
2. **Performing** — play a real (timed) set against music. Captures
   what's actually load-bearing under pressure.
3. **Recording a preset / capture session** — hunt for one good
   regime, then hold it. Captures the difference between "find" and
   "lock in" controls — these may be very different.

Don't merge them yet; analyze each separately first so the regime
shows in the data.

### Aggregations

The CSV is small enough that pandas / duckdb / a shell one-liner all
work. Useful slices:

```bash
CSV=~/Library/Application\ Support/Crutchfield\ Machine/usage_*.csv

# Top physical controls (which surface fired most events)
tail -n +2 "$CSV" | awk -F, '{print $2":"$3":"$4}' | sort | uniq -c | sort -rn | head -20

# Top actions (which app actions ran most — independent of source)
tail -n +2 "$CSV" | awk -F, '{print $6}' | sort | uniq -c | sort -rn | head -20

# Zero-use controls: list every binding in bindings.ini, subtract
# everything that appears in the action column. Whatever's left is
# the dead-weight set.
#   (do this in Python — too gnarly for awk)

# Magnitude distribution for an axis (does it use full range, or
# always nudge near center?)
grep ',cc:34:1,' "$CSV" | awk -F, '{print $7}' | sort -n | uniq -c

# Time-of-session bucket: when does action X tend to fire?
# (early-session = discovery, mid = performance, late = capture)
grep ',inject.hold' "$CSV" | awk -F, '{print int($1/60)}' | sort -n | uniq -c
```

### What to look for

Per-session and across sessions:

- **Zero-fire bindings.** If a default DDJ pad / key never fires
  across multiple sessions, its slot is wasted on the current action.
- **Single-fire bindings.** Hit once and never again = either
  exploration-only or wrong-key-for-the-action. Cross-reference
  against keyboard equivalents — if both keyboard and DDJ versions
  see one fire each, the action is probably underused on both.
- **Heavy axes with always-tiny magnitudes.** A CC that fires
  thousands of times but the magnitude histogram clusters near 0
  means the user wants finer resolution at center — change `scale`
  or use `delta` mode.
- **Heavy axes with magnitude saturated at ±max.** Opposite case —
  user wants more aggressive range. Scale up.
- **Actions with no fast binding.** Any action that fires often
  *only* via keyboard suggests it should also have a controller
  slot.
- **Adjacent surfaces with very imbalanced use.** If pad 1 fires 200×
  and pad 2 fires 0×, consider: are they wrong neighbors?

### Output of the review

A worksheet like:

| Physical surface | Current action | Fires (session 1 / 2 / 3) | Proposed action | Why |
|---|---|---|---|---|
| Deck-1 pad 2 (note 1, ch 8) | shape.star.hold | 0 / 0 / 0 | preset.next or preset.prev | dead default; presets are reached via Ctrl+N/P from keyboard |
| ... | | | | |

Decide cutoffs *after* you see the data — don't pre-commit to "kill
anything under N fires".

## Initial ideas (not commitments)

These are starting points only. Confirm or refute them against the
data before changing any defaults.

### Likely promotions

- **Layer toggles to DDJ pad rows.** Deck-2 already has the layer
  bank (notes 0–7 ch 10 → warp/optics/color/decay/noise/couple/external/inject)
  and that worked well in the first session — the LEDs make it obvious.
  Consider whether the *physics* + *thermal* toggles, currently behind
  the deck-2 shift bank, deserve to be promoted to unshifted slots if
  the shift bank sees little use.
- **Inject patterns to deck-1 unshifted pads.** Currently deck-1 pads
  1–4 are shape obstacle holds (which the first session showed are
  rarely used as separate shapes — pad 1 only). Replacing pads 2–4
  with quick inject-pattern selects (V-bars / dot / checker) might
  give faster regime priming.
- **Border decay nudge to a CC.** `borderDecay`, `borderSize`,
  `borderSoftness` currently only have keyboard bindings. They're
  hands-on shape-the-attractor controls and probably want to be on
  spare knobs/faders.
- **Sphere mode toggle on a deck button.** Currently `Alt+S` only.
  If sphere mode sees real use, give it a dedicated pad.

### Likely demotions

- **Shape obstacles 2–4** (star, circle, square) — first session: 0
  fires. Maybe demote to shift-bank, or remove from defaults
  entirely.
- **Master Cue bank pads 4–6** (noise scanline / noise layer toggle /
  inject layer toggle) — the underlying actions already have direct
  keyboard equivalents (`Home`, `F7`, `F10`) and dedicated deck-2 pad
  slots. Pad slots inside the master-shift bank may be better used
  for something else.

### Magnitude-tuning candidates

- **Jog wheels (CC 34 relative).** First session: 14k fires across
  both decks. That's the dominant input. Worth checking magnitude
  distribution — if it's always small relative deltas, the scale
  factor (currently 1.6–1.8) may be too sensitive.
- **CFX knobs (shape count / couple).** Low fire count in the first
  session. Hard to tell if they're underused because they don't get
  reached for, or because the action they drive isn't valued. Check
  whether the *keyboard* equivalents see use.

### Code-level changes that might fall out

- **Master-shift bank hardcoding** in `input.cpp` dispatch (lines
  ~1499–1512 macOS, ~1919–1929 Windows fork is gone now). Some of
  those pads may want to be repurposed; the special-case dispatch
  block is where to edit, not `bindings.ini`.
- **DDJ default-bindings block** in `input.cpp` (`installDefaults`,
  the `#ifdef __APPLE__` section). Anything mapped here is a built-in
  default; users override via `bindings.ini`. Adjusting defaults
  here is the right move for "what new users get out of the box".

## Out of scope

- Per-user binding sharing / profiles. Not needed yet.
- Hot-rebinding from inside the running app. Could be useful long-
  term, but the bindings.ini + restart loop is fast enough now.
- Action-level deprecation. We're tuning physical-control assignment,
  not removing actions.

## Open question

Should the master-shift bank become user-editable from `bindings.ini`
too? Right now it's hardcoded special-case dispatch in `input.cpp`
(noise modes + screenshots). Pros: discoverable in the bindings file.
Cons: another special case in the binding format. Decide this after
the data review tells us whether anyone wants to remap the bank.
