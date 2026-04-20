# ADR-0011 — Register as a virtual MIDI port (teVirtualMIDI) instead of launching loopMIDI's GUI

**Status:** Accepted
**Date:** 2026-04-20

## Context

For Strudel → feedback BPM sync and hit triggering, we needed an
OS-visible MIDI port that Strudel's browser-side Web MIDI could send
to. Original plan (captured in `development/plans/strudel_midi_sync.md`,
pre-work): winget-install loopMIDI, launch its GUI, the user creates a
port manually, feedback.exe opens it via winmm enumeration.

First-run UX of that path was "open a 2019-era dated-looking Windows
dialog, figure out which + button to press, type a name, enter". Simon
pushed back: "is this really the best way?"

Investigation revealed the underlying driver — `teVirtualMIDI64.dll`,
also from Tobias Erichsen — is shipped with loopMIDI, lives in
`C:\Windows\System32\` after install, and has a documented public C API
that any app can call to create a virtual port. No GUI interaction, no
click-the-+ step.

## Decision

feedback.exe loads `teVirtualMIDI64.dll` at runtime via `LoadLibrary` +
`GetProcAddress` and calls `virtualMIDICreatePortEx2("feedback", ...)`.

- **Port name** is fixed to `feedback`. Stable — Strudel patterns can
  hardcode `.midi("feedback")` and carry between machines.
- **Flags:** `TE_VM_FLAGS_PARSE_RX | TE_VM_FLAGS_INSTANTIATE_RX_ONLY`.
  PARSE_RX means the driver hands us complete MIDI messages (we don't
  have to reassemble running status). RX_ONLY means our port is
  receive-only from our POV — from Windows' MIDI subsystem POV it
  appears as a MIDI *output* destination, which is exactly what
  Strudel's Web MIDI wants to write to.
- **Fallback:** winmm enumeration path is retained in `input.cpp` as
  a fallback for users with hardware MIDI controllers plugged in but
  no teVirtualMIDI driver (e.g. they installed loopMIDI from a
  weird source, or someone runs on a box that never got loopMIDI).
  If the driver loads, the fallback is skipped to avoid
  double-counting events.
- **Dynamic load** is important: the driver is a Windows-only
  Tobias-Erichsen DLL. If it's not installed, `LoadLibrary` fails
  and we fall back to the winmm scan. The app boots fine without
  it; the user just doesn't see a `feedback` port in Strudel.
- **First-run install** — `Ctrl+M` or the startup picker's Music
  mode runs `winget install TobiasErichsen.loopMIDI` with UAC
  consent. This brings the driver along; the loopMIDI GUI is
  installed but never opened by us. The user gets the port without
  ever seeing it.

## Consequences

**Positive:**
- Zero-click setup after the one-time winget install. Port appears
  in Strudel automatically.
- Port name we control — "feedback" is recognisable.
- No nagging GUI window taking up a taskbar slot.
- Still works offline once installed — no network calls at runtime.

**Negative:**
- Windows-only. The teVirtualMIDI driver has no equivalent on Linux
  (ALSA seq is native but separate) or macOS (IAC driver, also
  different). Both are TODO; guarded by `#ifdef _WIN32`.
- Dynamic load adds a small failure surface (`GetProcAddress`
  returning nullptr). We handle it but it's one more branch.
- We're depending on Tobias Erichsen's loopMIDI installer to be
  available via winget. It is as of 2026-04; if it goes away, we
  fall back to winmm and users with hardware MIDI still work, but
  Strudel integration breaks for new users.
- License clarity: the teVirtualMIDI SDK itself is freely
  redistributable, but we don't ship it — we load the user's
  installed copy. That dodges any licensing complication cleanly.

## Alternatives rejected

- **rtpMIDI / network MIDI** — works cross-platform but requires
  extra config (Bonjour, IP discovery). Too much UX friction.
- **OSC via UDP** — would work, but Strudel's Web OSC path requires
  a Node bridge that we'd then need to install and manage. Adds
  moving parts.
- **Spawn a loopMIDI child process** — the original plan. Bad UX,
  dated window, extra process, user has to configure ports by
  hand.
- **Write our own MIDI driver** — massive scope, requires WHQL
  signing for Win10+. Not happening.
- **WebMIDI in an embedded WebView** — rejected same as ADR-0010:
  no browser in the loop.

## References

- teVirtualMIDI SDK: https://www.tobias-erichsen.de/software/virtualmidi.html
- Implementation: `input.cpp::Input::pollMidi` (the `te_open_port`
  path), and `main.cpp::music_ensure_driver`.
- Commit: `5805f15` on the `music` branch.
- Related: ADR-0010 (QuickJS), ADR-0012 (clean-room pattern engine).
