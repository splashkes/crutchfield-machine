# ADR-0006 — Show interactive console picker when launched with no CLI args

**Status:** Accepted
**Date:** 2026-04-19

## Context

With a statically-linked portable binary (ADR-0003) and growing CLI
surface (`--fullscreen`, `--demo`, `--precision`, `--preset`, ...),
there was friction for non-CLI users: double-clicking `feedback.exe`
from Explorer launched with defaults, leaving casual users unaware
that Gallery Mode, 4K Fullscreen, or 8-bit comparison modes exist.

Options for bridging the CLI / non-CLI gap:

1. **Pre-made Windows `.lnk` shortcuts** bundled in the zip, each
   launching with a different flag set. Idiomatic Windows, zero code.
   Shortcut files are opaque binary blobs — awkward to generate from
   Makefile, awkward to review in git.
2. **`.bat` launcher files** in the zip (`Demo.bat`, `Fullscreen.bat`).
   Simple text files; but double-clicking flashes a console window
   which looks janky, and users face a folder full of mystery files.
3. **In-app startup menu**, shown when launched with no args. Lets
   users discover modes inside the app itself. CLI users are unaffected
   because any flag (even `--help`) skips the menu.
4. **Full GUI launcher** (imgui / Qt dialog). Nicest UX, biggest code
   change, new dependency.

On a console-subsystem `feedback.exe`, double-clicking from Explorer
already opens a terminal window for stdout. We can use that window to
host a menu at zero dep cost.

## Decision

When `argc == 1` (no CLI arguments), show a short console menu before
`glfwInit()`:

```
  1  Default        windowed, moderate quality
  2  Fullscreen     borderless at native resolution
  3  Gallery mode   fullscreen + auto-cycle presets + inject
  4  4K Fullscreen  full float 3840x2160 sim
  5  8-bit study    RGBA8 feedback loop
  6  Load preset... pick from the N presets shipped
  Q  Quit
```

User types a digit (or Q), presses Enter, the selection modifies
`g_cfg` accordingly, and the app proceeds normally. Default on empty
input is option 1.

Any CLI flag (including `--help`) skips the picker so scripted
launches and terminal workflows are unaffected.

## Consequences

**Positive:**
- Zero new dependencies — uses existing console and `stdin`.
- Double-click users discover Gallery / 4K / 8-bit without reading
  docs first.
- CLI workflow is byte-for-byte unchanged — `feedback --fullscreen`
  behaves identically to before.
- Future modes are one `case` in the picker switch; scales naturally.

**Negative:**
- The menu lives in a console window, which isn't a polished-app
  experience. A Qt or imgui launcher would look nicer.
- Users on a headless-console build variant (if we ever make one)
  wouldn't have this path; would need a true GUI fallback.
- The "Load preset" sub-menu lists every preset alphabetically; if
  users accumulate 50 auto-saves, scrolling becomes awkward. Revisit
  if/when that's an actual user problem.

## Alternatives considered

- **Shortcut files in zip.** Considered, documented in earlier
  session discussion. Deferred — the in-app picker covers the same
  ground with less distribution machinery.
- **GUI launcher with imgui.** Rejected for v0.1 — adds a dependency
  and ~2× the work for marginal UX gain over a well-formatted console
  menu. Reconsider if a visual preset-browser branch lands; the two
  could share a front door.

## References

- `main.cpp: run_mode_picker()` — the picker implementation.
- `main.cpp: main()` — the `if (argc == 1) run_mode_picker(...)` gate.
- README.md — "Quick start" section describes the picker behavior.
