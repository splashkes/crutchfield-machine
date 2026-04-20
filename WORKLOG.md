# Worklog — 2026-04-19 session

Polish pass aimed at making the repo usable by people other than the author.

## Scope

- Hardware recording path (Elgato 4K60 Pro MK.2) investigated and documented.
- Binary distribution: go DLL-free so prebuilt releases actually run on a
  fresh Windows box.
- Extensibility: contributor doc + shader authoring guide.
- Power-user features: `--preset`, screenshot key, auto-demo mode.
- Platform honesty: annotated `linux/` and `macOS/` as WIP.

## Changes by area

### Recorder pipeline (earlier in session, already shipped)

Split recorder into reader + encoder thread pools connected by a RAM
buffer pool. New CLI: `--rec-ram-gb`, `--rec-encoders`, `--rec-uncompressed`.
Fixes the case where EXR ZIP encoders couldn't keep up with 4K60 capture.

Commits:
- `672fda7` — Split recorder into readback + encoder stages with RAM buffer pool
- `04f38ea` — Add research/ with source papers and feature inventory

### 8-bit feedback mode

Extended `--precision` from `{16, 32}` to `{8, 16, 32}`. `--precision 8`
runs the feedback loop in RGBA8 unorm so values clamp to [0,1] every pass
and quantize to 256 levels per channel — simulates what an 8-bit HDMI
capture (Elgato, OBS window-capture) does to the *dynamics*, not just to
the output frame. Different banding/attractor behavior than float.

### HDMI capture card investigation

Detected on the system: Elgato Game Capture 4K60 Pro MK.2 (PCIe,
`VEN_12AB`, driver `SC0710.X64`). No public SDK. The card is input-only
(HDMI IN + HDMI OUT passthrough). Three documented integration paths:

1. **Cable-only**: GPU HDMI → Elgato HDMI IN → Elgato HDMI OUT → monitor.
   Elgato 4K Capture Utility or OBS records it. Zero app changes.
2. **`--display N` flag** (not yet implemented): target a specific
   monitor so fullscreen lands on the capture display automatically.
3. **`obs-websocket`** (not yet implemented): backtick key triggers OBS
   recording in parallel with EXR. ~100 lines.

OBS's Windows Graphics Capture path is essentially equivalent to the
Elgato in quality (both 8-bit) but needs no cable or extra hardware — the
leading recommendation for most users.

### Static binary / distribution

**Makefile**: added `-DGLEW_STATIC`, switched to
`-Wl,-Bstatic -lglfw3 -lglew32 -Wl,-Bdynamic` so the linker picks the
`.a` archives over `.dll.a`. System DLLs (`opengl32`, `gdi32`, `mf*`) stay
dynamic — they're part of Windows.

New `make dist` target: builds, bundles `feedback.exe + shaders/ +
presets/ + README + LICENSE + CREDITS` into `feedback-windows-x64.zip`,
and runs `objdump -p` as a sanity check that no MSYS2 DLLs snuck into the
import table. This is what gets uploaded to GitHub Releases.

### Preset CLI flag

`--preset NAME` loads a preset at startup. Accepts:
- bare stem (`03_turing_blobs`)
- filename (`03_turing_blobs.ini`)
- absolute or relative path

Non-fatal if not found (prints a warning, falls back to defaults).

### Screenshot key

`PrtSc` key writes a PNG of the current sim-resolution frame to
`./screenshots/shot_<timestamp>.png`. Reads from the displayed field's FBO
(not the display framebuffer), so screenshots are at full sim quality and
never include the HUD overlay. Uses `stb_image_write.h` (added to repo,
public domain).

### Auto-demo mode

- `--demo-presets S` — cycle to next preset every S seconds
- `--demo-inject S` — fire a randomly-chosen injection pattern every S seconds
- `--demo` — shortcut for `--demo-presets 30 --demo-inject 8`

Both timers independent; set either to 0 to disable. Intended for gallery
installations, background monitors, screen-saver-style use.

### Documentation

- **`CONTRIBUTING.md`** (new): project layout, step-by-step "add a new
  shader layer" walkthrough using a `sharpen` example, shader contribution
  ideas (sobel, bloom, kaleidoscope, bit-crush, displacement warp, bloom,
  etc.), host-side ideas (audio reactivity, MIDI, OSC), and infra ideas
  (CI, release automation).
- **`linux/README.md`** (new): marks the Linux subdir as proof-of-concept;
  lists what's missing vs. the Windows build.
- **`macOS/README.md`** (new): marks the macOS subdir as not-started;
  lists planned approach.
- **`README.md`** (root): added "Quick start — prebuilt binary" section,
  `make dist` step in the build instructions, Preset / Auto-demo sections
  in the CLI cookbook, `PrtSc` / recording / preset keys in the Controls
  table, Contributing + Background sections at the bottom.

### Housekeeping

- `.gitignore`: `screenshots/`, `feedback-windows-x64/`, `feedback-windows-x64.zip`.

## Files touched

### New
- `CONTRIBUTING.md`
- `WORKLOG.md` (this file)
- `linux/README.md`
- `macOS/README.md`
- `stb_image_write.h` (third-party, public domain)

### Modified
- `Makefile` — static linking, `dist` target
- `README.md` — prebuilt binary quickstart, expanded CLI cookbook,
  controls table, contributing link, background link
- `main.cpp` — `--precision 8`, `--preset`, `--demo*`, screenshot key,
  stb_image_write include, State fields for demo timers + screenshot
  flag, config pass-through
- `.gitignore` — screenshots, dist bundle

## Known pending / follow-ups

### Must fix before release

- **EXR recorder exit hang** — on shutdown with a full RAM buffer, stop()
  synchronously joins encoder threads while they drain to disk. Window
  goes Not Responding for up to ~30s at 4K60, looking like a crash. Fix
  requires splitting stop() into `beginStop()` (non-blocking) and
  `finishStop()`, then polling from the main loop while showing a
  "Saving recording" overlay. Scoped but not implemented this session.

### Nice-to-have (from the polish list)

- Screenshots / demo GIF in the README (needs to be recorded — can't do
  automated).
- Non-empty default startup state (load `01_default.ini` at launch).
- Troubleshooting section in README (black screen, 4K slow, recorder
  drops, no camera).
- GitHub Actions CI for cross-platform builds.
- `presets/README.md` describing the curated presets.
- `--display N` flag for targeting a specific monitor (matters for the
  Elgato/OBS workflow).
- `--record-on-start` for unattended-capture sessions with `--demo`.

### Architecture

- Preset menu overlay (in-window browser) — another contributor is
  working on this in a separate worktree per user note.
- Audio reactivity (WASAPI loopback + RMS/band envelope) — flagged as
  coming soon in CONTRIBUTING.md.

## Verification owed

Not executed in this session — user to run in their MSYS2 shell:

```bash
make clean && make
./feedback.exe --demo
./feedback.exe --preset 03_turing_blobs
# press PrtSc to screenshot

make dist
# sanity-check the objdump output: should list only Windows system DLLs
```

The bash environment available during this session had a `TMP`
permissions issue that prevented me from driving `make` directly.
