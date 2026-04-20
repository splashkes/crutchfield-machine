# Edirol V-4 — A/B bus features held back for a future pass

## Context

The first V-4 integration pass (branch `roland-v4`) kept the feedback
system single-bus. The real V-4 is a **two-bus mixer** with a T-bar
crossfade between independent A and B signal chains. Most of the V-4's
character — wipes, keyed transitions, Transformer button matrix,
Presentation mode, V-LINK — only makes sense once both buses exist.

This doc enumerates what we **deferred** in that first pass, and why,
so a future pass can pick it up without rediscovering the design.

## What shipped on the single-bus pass

- Two V-4 effect slots (scaffolds the A-slot / B-slot gesture but both
  operate on the same single output).
- 18 effects covering the core families: Strobe, Still, Shake,
  Negative, Colorize, Monochrome, Posterize, ColorPass, Mirror-H/V/HV,
  Multi-H/V/HV, W-LumiKey, B-LumiKey, ChromaKey, PinP.
- Output fade (the V-4's Output Fade dial) as a bipolar control.
- BPM sync + tap tempo with five beat-locked modulations.
- Per-slot "B-source" selector for key/PinP families — either the
  camera or the current feedback frame itself. Not a second bus; just
  an alternate source for the compositing effects.

## What was held back

### 1. A/B bus architecture

**What it is.** Each bus is an independent feedback field with its own
Params: warp, color, dynamics, effects, etc. The T-bar selects which
bus (or a blend) reaches the program output. Bus B is not "the other
field in a CML" — it's a full parallel pipeline.

**Why held back.** The app's `Params` struct today is single-bus. Promoting
it to `Params bus[2]` is a large refactor touching every uniform setter,
every preset key, every action handler, and the HUD layout. It's the
right architectural move for a faithful V-4 port but was out of scope
for the first integration pass.

**Implementation sketch.**
- Extend `Params` into `BusParams` + a top-level `Master` for T-bar,
  output fade, BPM. Two `BusParams bus[2]` in `State`.
- Duplicate `render_field` into a bus-driven variant; the existing 4-field
  coupling stays available inside a bus (Kaneko CML lives *inside* bus A).
- Actions that currently target a param need a bus-select prefix
  (`a.warp.zoom+` vs `b.warp.zoom+`), or an active-bus concept that the
  UI surfaces (`Shift-A` / `Shift-B` focus a bus).
- Shader: existing `main.frag` runs per-bus. After both buses render
  to their own textures, a `combine.frag` reads both + `uTbar` and
  outputs the program frame.

### 2. Wipe bank (216 wipes)

**What it is.** The Mix/Wipe/EFX transition buttons on the V-4 select
one of 248 transitions: 1 Mix + 216 Wipes (72 hard / 72 soft / 72
multi-border) + 4 Key + 27 Slide.

**Why held back.** No A/B bus, so no transitions. And 216 wipes is a
lot of shader work — most would be visually similar in a feedback
context.

**Implementation sketch.**
- Procedural wipe mask generator, not exhaustive catalogue. ~12 base
  shapes × 3 edge modes (hard / soft / multi-border) × configurable
  angle/progress = covers most of the V-4's space with ~36 variants.
- Each wipe is a `float mask(vec2 uv, float progress, int shape, int edgeMode, float angle)` returning [0,1]; T-bar position drives `progress`.
- EFX bank (4 key transitions + 27 slides) layers on top — key
  transitions use the existing W-LumiKey / B-LumiKey / ChromaKey
  extractions as the wipe's α. Slides are geometric.

### 3. Transformer button matrix

**What it is.** Two Transformer buttons (one per bus) that perform one
of 36 actions per memory slot. Most actions are "force output to A/B
with transition duration D" or "A↔B toggle with duration D" — they only
exist once there's an A↔B distinction.

**Why held back.** Same reason as wipes — no A/B bus.

**Kept in the single-bus pass.** Only the four fade-to-color entries:
White-White, Black-Black, White-Black, Black-White. These map cleanly
to the existing Output Fade control. The remaining 32 Transformer
entries live in the held-back pile.

**Implementation sketch.** Once A/B is in:
- Transformer action = `{ targetBus, transitionFrames }`. Action table
  of 36 per-bus entries, one active per memory slot.
- On press, the T-bar motion is synthesised over `transitionFrames`.
  Duration digit 0 = hard cut, 9 = slowest — we'll need to measure the
  real V-4 to pin frame counts.

### 4. Memory slots 1–8

**What it is.** A panel-configuration ring of 8 slots that store effect
assignments, transitions, Transformer behaviours, BPM settings, etc.

**Why held back.** Existing INI-file preset system covers the creative
state. User explicitly said "no additional memory" when asked.

**Possible revisit.** Once A/B lands, instant-recall slots feel more
compelling because a panel config is bigger and performative swaps are
more interesting. At that point, add an in-memory 8-slot ring that
sits *in addition to* the file presets, with a hardware-controller
Memory dial mapping.

### 5. Preview bus

**What it is.** V-4's second composite output showing either a chosen
input, the program output, or an auto-cycling preview.

**Why held back.** We have a single display window and no analogous UI
for a second preview pane. Auto-cycle wasn't meaningful.

**Implementation sketch.** A second GLFW window (we already avoided
this for the help panel for good reasons — fragile on Windows), or an
in-window picture-in-picture of the camera / alternate bus when
enabled. The PinP vfx effect already does something close to this.

### 6. Presentation mode

**What it is.** A mode switch that repurposes the front panel for a
lecture-style workflow — memory slots become pre-baked transitions,
T-bar becomes a B-level fader instead of a crossfade, BPM indicator
shows transition time, etc.

**Why held back.** It's a front-panel behavioural skin, useful when
you're performing with the unit itself but non-obvious on a software
instrument with a different control surface.

### 7. V-LINK

**What it is.** A Roland proprietary layer over MIDI that synchronises
video devices with audio/music devices. The V-4 MIDI Implementation
Chart reserves SysEx addresses for it (model ID 00h 51h).

**Why held back.** We're doing MIDI architectural hooks only in this
pass. V-LINK sits on top of a working MIDI implementation.

**Implementation sketch.** Once MIDI is real (RtMidi or winmm), the
V-LINK layer adds:
- Recognise V-LINK SysEx messages and map them to the same action
  registry the local input layer drives.
- Possibly emit V-LINK SysEx so the feedback app can be a source for
  other V-LINK devices.

### 8. Full MIDI implementation

**What it is.** V-4 MIDI v1.00 (Dec 2002) — CC-mapped controls, Note-On
triggers, Program Change for presets, bank select.

**Why held back.** User asked for "architectural hooks only — eventually
a MIDI controller with knobs." The action registry and bindings.ini
already accept `[midi] cc:N` entries syntactically, but `Input::pollMidi`
is a stub.

**Implementation path.** Add RtMidi dependency (cross-platform) or use
winmm directly on Windows. Poll messages each frame in
`Input::pollMidi`, translate (CC, channel) into the same dispatch the
keyboard/gamepad use. Support learn mode (press a control on the
device then a keyboard key → write the binding). MIDI Implementation
v1.00's per-control CC mapping can drive the default [midi] section.

## Implementation order when we revisit

1. A/B bus refactor (the foundation for nearly everything else).
2. MIDI real (unlocks controller-driven work).
3. Wipe bank (procedural, ~36 variants).
4. Transformer matrix (depends on A/B + transitions).
5. V-LINK (depends on MIDI).
6. Memory slots 1–8 (worth doing once there's more state to store).
7. Preview bus (nice-to-have).
8. Presentation mode (nice-to-have, low priority).
