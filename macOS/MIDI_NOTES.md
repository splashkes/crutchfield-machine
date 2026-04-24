# macOS MIDI / DJ controller — planning notes

Pre-implementation notes for adding MIDI **input** from a DJ controller, with
macOS as the first target. Not a finished design.

## What already exists in the codebase

- `bindings.ini` has a `[midi]` section parsed in `input.cpp` with `cc:N`
  mappings, channel filtering, and a port-name hint (`setMidiPortHint`).
- ADR-0011 covers the Windows side: register as a virtual MIDI port via
  teVirtualMIDI to receive from Strudel.
- ADR-0014 already establishes the platform-transform pattern (mac/linux
  files behind a thin C interface, e.g. `camera_avfoundation.mm`).

So the action-routing layer is in place. The new work is a **platform MIDI
backend** plus a few DJ-specific message shapes the existing `[midi]`
parser does not yet model.

## macOS backend shape

- Use **CoreMIDI** directly. No driver shim needed (unlike loopMIDI on
  Windows). New file `macOS/midi_coremidi.mm` paralleling
  `camera_avfoundation.mm`, behind a small C interface that `input.cpp`
  can call from shared code.
- Calls: `MIDIClientCreate` → `MIDIInputPortCreate` →
  `MIDIPortConnectSource`. Enumerate sources with
  `MIDIGetNumberOfSources()` and match by `kMIDIPropertyName` against the
  port hint, mirroring the Windows behavior.
- Test against the **IAC Driver** (built-in virtual port, enable in
  Audio MIDI Setup) before plugging in real hardware. Decouples
  CoreMIDI bring-up from controller quirks.
- CoreMIDI callbacks fire on a high-priority thread. Push events into a
  lock-free queue and drain on the main loop — do **not** run actions
  inline from the callback.
- No entitlements/permissions needed for the CLI binary. Only matters if
  bundled and notarized later.

## DJ-controller-specific gotchas

These bite on a DJ controller but not on a generic MIDI keyboard, and the
current absolute-CC mapping does not cover them:

- **Jog wheels send relative CC** — signed-bit or two's-complement around
  `0x40`, not absolute 0..127. Without handling this, scrubbing reads as
  a slider snapping between two values. Add a per-binding
  `mode=relative` option, or normalize in the platform layer.
- **14-bit CC pairs** — better crossfaders / pitch faders send MSB on
  CC# and LSB on CC#+32. Optional, but the difference between smooth
  and stepped on pitch.
- **Shift / bank modifiers** — typically the hardware sends the same pad
  on a different channel or a different note when shift is held. Let
  the hardware do it; bind both rows in `bindings.ini`. Avoid modeling
  modal state in software.
- **Message volume** — jog scrubbing is hundreds of msgs/sec. Coalesce
  same-CC events when draining the queue per frame.
- **Class compliance varies** — some controllers (Numark, Hercules) are
  USB-MIDI class compliant and just work. Others (some Pioneer DDJ,
  Traktor units) need a vendor driver and/or a "MIDI mode" toggle to
  expose generic MIDI rather than a vendor protocol.

## LED feedback (bidirectional) — defer

DJ controllers light pads / VU meters via Note-On (or SysEx) sent **back
to the controller**. This is desirable eventually but roughly 3× the work
and changes the abstraction (`midi_open` → `midi_open_io`).

Decision: ship **input-only** first, add output later once the mapping is
stable.

## First concrete deliverable: MIDI-learn mode

Highest-leverage thing to build on day one. A `--midi-learn` flag (or a
hotkey) that prints every incoming message as `ch=N cc=NN val=VV` (and
the analogous form for note-on / note-off). The fastest path from
"unboxed controller" to "fully mapped" is twiddling each control while
watching the log and pasting into `bindings.ini`. Skipping this turns
mapping into a weekend of cross-referencing manufacturer PDFs.

## Build-time wiring

`scripts/prepare_sources.py` already transforms shared root sources at
build time. The new `.mm` file is mac-specific and lives in `macOS/`,
joined into the link step alongside `camera_avfoundation.mm`. Likely no
changes needed in the source-prep transform itself — only the Makefile
link list.

## Open questions to resolve before coding

- Which controller, specifically? Determines whether we get class-
  compliant MIDI for free or need to hunt for a vendor driver.
- Do we want jog-wheel handling in shared code or in the macOS backend?
  Shared is cleaner; backend is faster to land.
- Is the Linux port going to follow with ALSA seq, and if so does the
  C interface need to be designed for three backends now or two?
