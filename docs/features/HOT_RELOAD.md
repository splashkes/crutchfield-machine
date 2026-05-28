# Hot reload — edit bindings.ini live

`bindings.ini` reloads automatically when its mtime advances OR when Crutchfield receives SIGHUP / SIGUSR1. No restart, no lost GL context, no torn-down OSC port.

## How it works

Each frame, `Input::tryReload(dt)` runs after the input pollers. It:

1. Accumulates `dt` and stat()s the bindings file once per second (1 Hz cadence).
2. If the file's mtime is newer than the last loaded mtime, OR if a SIGHUP/SIGUSR1 set the `reloadRequested_` flag, performs a full reload.
3. A full reload = `clear()` (drop all bindings) → `installDefaults()` (restore keyboard + MIDI defaults) → `loadIni(path)` (re-parse the file).
4. Macros, audio bindings, link bindings, OSC bindings — all rebuilt.

State that survives a reload:
- Engine state (Params, enable mask, FBO contents)
- OSC listener (socket stays bound)
- MIDI device connection
- Audio analyzer state
- Ableton Link session (peers, tempo)
- State snapshots (g_snapshots[1..8])
- Math dashboard ring buffer

State that gets reset:
- Binding dispatch state (last-value caches for OSC delta mode)
- Macro registry (re-registered from the file)

## Triggering a reload

```bash
# Auto-detect: just edit ~/Library/Application Support/Crutchfield Machine/bindings.ini
# Within 1 second the reload fires automatically.

# Explicit: send SIGHUP to the running process
kill -HUP $(pgrep -f crutchfield-machine/feedback)

# Or SIGUSR1 — same handler
kill -USR1 $(pgrep -f crutchfield-machine/feedback)
```

Output:

```
[bindings] hot-reload triggered for bindings.ini
[bindings] reload ok — 431 bindings active
```

If the file fails to parse cleanly:

```
[bindings] reload fallback to defaults — 230 bindings active
```

(The keyboard + MIDI defaults still load even if the file is malformed; you don't end up with zero bindings.)

## Why the 1-second throttle

stat() is cheap but not free, and on some filesystems mtime granularity is per-second. Polling every frame at 60 Hz would be wasteful and produce no benefit. 1 Hz polling means the worst-case lag between a save and a reload is ~1 second, which is imperceptible in a live performance context.

The SIGHUP path bypasses the throttle — if you need instant reload (e.g. from a script), `kill -HUP` fires the next frame.

## Live performance workflow

Open `bindings.ini` in your editor on a second monitor. Crutchfield runs on the main display. Edit a binding, save the file. Within 1 second the change is live. No interruption to the running feedback simulation.

This is the workflow that makes the OSC + macro layer actually useful — you can iterate on mappings during a sound check without restarting and losing your simulation state.

## Limitations

- Reloads always clear and reload; there's no incremental "just the OSC section" path. If you have 500 bindings the reload takes a few ms (still imperceptible).
- The OSC listener port can't be changed at reload time without re-binding. Setting a new `listen = PORT` in the `[osc]` section will close the current socket and open the new one — there's a brief gap where messages won't arrive.
- Hot reload doesn't reload shaders. Use `app.reloadShaders` action for that.

## Implementation pointers

- `Input::rememberBindingsPath()`, `Input::tryReload()` in `input.cpp`
- SIGHUP handler installed in `main.cpp` near `g_input.installDefaults()`
- The per-frame call site is in the main render loop alongside `pollMidi`, `pollOsc`, `pollAudio`, `pollLink`
