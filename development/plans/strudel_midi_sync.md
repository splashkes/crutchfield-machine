# Strudel ↔ feedback integration — research notes

> **Status: SHIPPED in v0.1.3 (2026-04-20).** See ADRs 0010–0013 for
> the decisions that came out of this planning round. The original
> plan below is preserved for historical reference; the actual
> implementation diverged in three important ways:
>
> 1. We register as a virtual MIDI port via teVirtualMIDI instead of
>    opening an existing loopMIDI port (ADR-0011).
> 2. We built a native standalone music engine rather than only
>    syncing to an external Strudel instance (ADRs 0010 + 0012).
> 3. The scheduler advances on frame dt, not wall time (ADR-0013),
>    for correctness under alt-tab and extension to visual coupling.
>
> Both directions of integration now exist: Strudel → feedback via
> the `feedback` virtual MIDI port, and standalone native playback
> from `music/*.strudel` files with the visual feedback state
> exposed as `fb.*` globals.

## What Strudel is

[Strudel](https://strudel.cc) is a JavaScript port of TidalCycles — a
live-coding language for algorithmic music patterns. It runs in a
browser REPL; patterns can be re-evaluated on the fly without stopping
playback. It has MIDI, OSC, and MQTT output, plus a small amount of MIDI
input support.

Source: [codeberg.org/uzu/strudel](https://codeberg.org/uzu/strudel).

## Goal

1. Strudel drives our feedback app's BPM and triggers visual events on
   drum hits / other pattern events.
2. (Stretch.) Our tap-tempo updates Strudel's cps so the two can be
   "locked" even when Strudel is stopped or between pattern evaluations.

## What Strudel can do

### MIDI OUT (reliable, supported)

`.midi("<port-name>")` sends to any OS-visible MIDI output port via the
Web MIDI API. On Windows, routed through a virtual port (loopMIDI,
LoopBe1, etc.).

Supported message types:

| Strudel call              | MIDI message                | Notes |
|---------------------------|-----------------------------|-------|
| `s("bd sn").midi(port)`   | Note-On / Note-Off          | Default vel 0.9, ch 1 |
| `.midichan(N)`            | Sets channel 1–16           | |
| `.ccn(N).ccv(v)`          | Control Change              | Patterned values OK |
| `.control([cc, pattern])` | Shorthand for CCN+CCV       | |
| `.progNum(n).midi()`      | Program Change              | 0..127 |
| `.midibend(x)`            | Pitch Bend                  | −1..+1 |
| `.miditouch(x)`           | Channel Aftertouch          | 0..1 |
| `midicmd("clock*24")`     | System real-time clock tick | Pattern-driven |
| `midicmd("<start stop>")` | System real-time start/stop | Fires on eval |
| `.sysex(id, data)`        | SysEx                       | |

`midicmd("clock*24*2,<start stop>/2").midi(port)` sends MIDI Clock at
24 PPQN (the standard), with start/stop alternating each half-cycle.
**Start is auto-fired when the REPL begins**, so phase alignment is
handled automatically.

Option bag on `.midi()`:

| option        | default | meaning |
|---------------|---------|---------|
| `latencyMs`   | 34      | Aligns MIDI send with audio output |
| `noteOffsetMs`| 10      | Note-off latency |
| `velocity`    | 0.9     | Default velocity |
| `midichannel` | 1       | Default channel |
| `midimap`     | 'default' | Custom CC-name mapping |
| `isController`| false   | Disables Note messages (for CC-only streams) |

### MIDI IN (partial)

- `midin("<port>")` — returns a function to query CC values. CC-driven
  params: `note("c d e").lpf(await midin(port)(74).range(0, 1000))`.
- `midikeys("<port>")` — receives Note-On events. Note lengths are
  fixed because of SuperDough constraints.

**Strudel does NOT follow incoming MIDI Clock.** There's no `midisync`
or similar; `midicmd("clock")` is output-only.

An [Ableton Link PR (#719)](https://codeberg.org/uzu/strudel/pulls/719)
attempted bidirectional sync but was closed in Sep 2025 because it
depended on Tauri which upstream is dropping. Nothing has replaced it
yet.

### OSC / MQTT

Strudel can output OSC via a small Node bridge (ships with the repo)
and MQTT directly. Both require more moving parts than MIDI and neither
solves the "follow external clock" problem either.

## What this means for our integration

The clean, reliable path is **Strudel → us (MIDI)**:

```
Strudel (browser)
  └── Web MIDI API
        └── loopMIDI virtual port ("strudel→feedback")
              └── winmm (our app)
                    └── Input::pollMidi → ActionId dispatch
```

- **MIDI Clock (0xF8)** at 24 PPQN → derive BPM; override p.bpm while
  the clock stream is live. This replaces tap-tempo when Strudel is
  driving.
- **Start (0xFA) / Stop (0xFC)** → anchor the beat-0 phase; pause our
  internal beat clock when Strudel stops.
- **Note-On (0x9n)** → fire named actions by (channel, note) tuple
  mapped in `bindings.ini [midi]`. e.g. channel-10 kick drum note 36
  → `bpm.flash`; note 38 → `inject.hold`; note 42 → `vfx1.next`.
- **CC (0xBn)** → drive continuous parameters. e.g. CC 1 (mod wheel)
  → `warp.zoom.axis`, CC 74 → `color.hueRate`.

The **us → Strudel** direction is limited:

- Strudel doesn't follow MIDI Clock in, so sending MIDI Clock out of
  our app to Strudel is inert.
- BUT we can send CC to Strudel, and the user's live-coded Strudel
  program can pick it up via `midin()` and drive `setcps()`:

  ```javascript
  // Strudel side
  const fb = await midin('feedback→strudel');
  // map our CC 20 (0..127) to cps 0.3..1.0
  setcps(fb(20).range(0.3, 1.0))
  ```

  This requires the user to write Strudel code that reads the CC and
  chooses how to apply it. It's not automatic sync. And whether
  `setcps()` accepts a dynamic pattern (not just a static value) is
  unconfirmed — Strudel docs don't document this case. If it doesn't,
  we'd fall back to "user re-evaluates when they want to pick up the
  new tempo", which defeats the point.

**Conclusion**: ship direction (Strudel → us) as the primary sync path.
Include the (us → Strudel) CC output as an opt-in extra, documented
with a working Strudel snippet. If Strudel grows a real follow-clock-in
someday (or Ableton Link lands upstream), we can revisit.

## Implementation plan (post-merge commit)

**Scope**: one commit, contained.

### 1. Real MIDI input (winmm, Windows)

Replace `Input::pollMidi` stub with a real implementation:

- Open a MIDI input device by name matching (e.g. "loopMIDI Port" or
  user-specified).
- Register a callback via `midiInOpen(..., MIDI_CALLBACK_FUNCTION)`.
  The callback runs on a system thread, so push raw bytes into a
  thread-safe SPSC queue.
- `pollMidi(dt)` drains the queue on the main thread and dispatches
  ActionIds through the existing handler callback (same surface the
  keyboard and gamepad already use).

Helper: keep a ring buffer of recent clock timestamps to derive BPM.
Every 24th clock pulse = one quarter note; BPM = 60 / (24 clocks' worth
of time / 24). Smooth across a few beats so jitter doesn't jump the
display.

On `Start`: reset `beatOrigin` to now, ensure `bpmSyncOn = true`.
On `Stop`: optional — leave sync on but tempo frozen, or flip sync off.
I lean **leave on, frozen**, so visual beat events stop but the app
doesn't lose its settings.

### 2. Binding surface

`bindings.ini` already accepts `[midi]` with `cc:N`. Extend for notes:

```ini
[midi]
port          = loopMIDI Port          # substring match OK
clock         = follow                  # follow | ignore

bpm.flash     = note:36 ch=10          # kick drum
inject.hold   = note:38 ch=10          # snare
vfx1.next     = note:42 ch=10          # hat
warp.zoom.axis = cc:1                  # mod wheel
color.hue.axis = cc:74                 # filter CC
```

New parse tokens: `note:N`, optional `ch=N` (1..16). Default channel
any (omni). Note velocity passed as the dispatch magnitude so patterns
with dynamic velocity drive the action strength naturally.

### 3. Reverse direction (optional)

Add a MIDI **output** port (same winmm, via `midiOutOpen`). Send a CC
whenever our BPM changes (e.g. CC 20 = BPM/2.5 clamped to 0..127,
channel 16). Document the Strudel-side receiver snippet in the new
help section.

User chooses port name via `bindings.ini`:

```ini
[midi]
port     = loopMIDI Port A → feedback
out_port = loopMIDI Port B → strudel    # optional
```

### 4. Help panel: new MIDI section

Between BPM and Quality in the HELP_SECTIONS order. Shows:
- Input port name + connected/disconnected state
- Last Clock derived BPM (with rolling avg)
- Beats/bars since Start
- Last received Note (channel, number, velocity) and CC (number, value)
- Output port name + state
- Last outgoing CC value + timestamp

No contextual gamepad map for this section (it's informational).

### 5. Failure modes

- loopMIDI not running / port not found → print warning, continue
  without MIDI. Don't hard-fail.
- Port disappears mid-run (user kills loopMIDI) → poll
  `midiInGetNumDevs()` periodically; on loss, close and retry every
  few seconds.
- Clock starves (no pulses for ~500ms) → treat as stopped; user tap
  regains control.

### 6. Non-Windows

Linux/Mac: use ALSA seq / CoreMIDI. Not scope for this pass; guard the
winmm block with `#ifdef _WIN32` and leave a TODO.

## Open questions for the implementation commit

1. **Default MIDI port name** to search for — just try any port ending
   "feedback"? List all ports on startup so the user can pick? Have
   the user name it in `bindings.ini`. I'd default to picking the
   first port whose name matches `*feedback*` case-insensitive, else
   the first loopMIDI port, else log "no MIDI input configured".

2. **BPM-derived vs manual** — when MIDI Clock is streaming, should
   tap-tempo do nothing, or snap Strudel's clock to a tapped
   interpretation? I'd say Clock wins; tap tempo becomes inert while
   Clock is live. Legend in the BPM section updates to say "(MIDI
   clock locked)".

3. **Strudel start/stop reach** — should Strudel's Stop command also
   turn off our `bpmSyncOn` toggle? Or freeze it? I lean **freeze**:
   keep modulations set, but since no beats fire, nothing happens
   until Start resumes.

4. **Velocity as magnitude** — Note-On velocity (0..127) fed as the
   dispatch magnitude lets patterns with velocity-dynamics drive
   action "strength" naturally. Applies cleanly to things like
   `outfade.up` or a flash action, less so to pure toggles.

5. **Sysex or CCs for bidirectional?** SysEx is universal but
   requires Strudel-side code to parse. CC is simpler. Stick with CC
   for v1.

## Strudel-side example (for the eventual README)

```javascript
// Send clock + drum pattern to our feedback app.
stack(
  // Audible drums
  s("bd*2, ~ sn, hh*8").bank("RolandTR909"),

  // Same pattern mirrored to MIDI for the feedback sync
  s("bd").note(36).channel(10).midi("loopMIDI Port"),
  s("sn").note(38).channel(10).midi("loopMIDI Port"),
  s("hh*8").note(42).channel(10).midi("loopMIDI Port"),

  // 24 PPQN clock so feedback follows our tempo
  midicmd("clock*24,<start stop>/2").midi("loopMIDI Port"),
)
```

```javascript
// Receive our BPM tap, drive setcps accordingly (unconfirmed — may
// require re-eval on each tempo change if setcps is static-only).
const fbBpm = await midin("loopMIDI Port B");
setcps(fbBpm(20).range(0.3, 1.0))
```

## References

- [Strudel input/output docs](https://strudel.cc/learn/input-output/)
- [Strudel cycles concept](https://strudel.cc/understand/cycles/)
- [PR #710 — MIDI clock output (merged)](https://codeberg.org/uzu/strudel/pulls/710)
- [PR #719 — Ableton Link (closed)](https://codeberg.org/uzu/strudel/pulls/719)
- [loopMIDI by Tobias Erichsen](https://www.tobias-erichsen.de/software/loopmidi.html) — the virtual-port bridge we'll recommend on Windows
