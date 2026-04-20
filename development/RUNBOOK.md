# RUNBOOK — build, test, release, troubleshoot

Exact commands for every operational procedure. If something you do
regularly isn't in here, add it.

## Prerequisites (one time)

**MSYS2 on Windows:**

```bash
# In MSYS2 MINGW64 shell
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw \
                   mingw-w64-x86_64-glew make
```

**Optional for releases:**

- GitHub CLI (`gh`) authenticated to the repo. Check with `gh auth status`.
- `pdftotext` from `mingw-w64-x86_64-poppler` if you need to quote
  research papers.

## Build (Windows — reference build)

```bash
make                   # incremental
make clean && make     # full rebuild
```

Expected output includes one warning from `stb_easy_font.h` about an
unused function — harmless. Any other warning is worth investigating.

## Build (Linux — Ubuntu / Debian)

The Linux port is a transform over the Windows source tree — no forked
`main.cpp`. See [ADR-0014](ADR/0014-platform-transforms-for-mac-and-linux.md).

```bash
sudo apt install build-essential python3 pkg-config \
                 libglfw3-dev libglew-dev libgl1-mesa-dev \
                 libasound2-dev zlib1g-dev ffmpeg
cd linux
make                        # incremental
make clean && make          # full rebuild
make dist                   # feedback-linux-x64.tar.gz
```

Run from the resource tree so cwd-relative asset loads resolve:

```bash
cd linux/build/resources
../bin/feedback
```

Prep-time failures that name a specific snippet (e.g. "couldn't find
expected snippet for ffmpeg probe null device") mean a Windows-source
edit drifted the patch target. Fix `linux/scripts/prepare_sources.py`
to match the new snippet shape; the Windows source is canonical.

## Build (macOS — Apple Silicon)

Same transform pattern as Linux, plus `.app` bundling and ad-hoc
codesigning.

```bash
brew install glfw glew
cd macOS
make                        # builds build/bin/feedback, then feedback.app
make dist                   # feedback-macos-arm64.zip with the .app inside
```

The `.app` bundle relocates `libglfw.3.dylib` and `libGLEW.2.3.dylib`
into `Contents/Frameworks/` via `install_name_tool`, so end-users don't
need a Homebrew install at runtime.

## Run

```bash
./feedback.exe                      # picker shown (argc == 1)
./feedback.exe --fullscreen         # picker skipped
./feedback.exe --demo               # gallery mode
./feedback.exe --help               # flag reference
```

See README.md for the full flag cookbook.

## Package (make dist)

Produces `feedback-windows-x64.zip` — the distribution artifact.

```bash
make clean && make dist
```

This runs:
1. Clean + rebuild.
2. Stage `feedback.exe + shaders/ + presets/ + js/ + music/ + samples/
   (if present) + README + LICENSE + CREDITS` into
   `feedback-windows-x64/`. The `js/engine.js` and `music/*.strudel`
   files must be present at runtime — don't strip them from the zip.
3. Run `objdump -p feedback.exe | grep "DLL Name:"` — the import table
   check. **Verify the output contains only Windows system DLLs:**

   ```
   GDI32.dll KERNEL32.dll MF.dll MFPlat.DLL MFReadWrite.dll
   msvcrt.dll ole32.dll OPENGL32.dll SHELL32.dll USER32.dll WINMM.dll
   ```

   If `libwinpthread-1.dll`, `libstdc++-6.dll`, `glfw3.dll`,
   `glew32.dll`, or `zlib1.dll` appears — static linking broke. See
   the Makefile's LDFLAGS/LDLIBS comments. Fix before shipping.
4. PowerShell `Compress-Archive` → `feedback-windows-x64.zip`.

### If you ship a macOS binary

Do not assume a `feedback-macos-arm64` zip is self-contained just because it
launches on the build machine. Before attaching any macOS artifact to a
release, inspect it:

```bash
otool -L ./feedback
```

If the output includes Homebrew paths such as:

```text
/opt/homebrew/opt/glfw/lib/libglfw.3.dylib
/opt/homebrew/opt/glew/lib/libGLEW.2.3.dylib
```

then a fresh Apple Silicon machine will fail on first launch unless the user
installs:

```bash
brew install glfw glew
```

Release rule: either bundle/relocate those dylibs into the macOS artifact, or
state the Homebrew runtime prerequisite explicitly in the release notes and
README. Do not ship an undocumented dynamic dependency.

## Cut a release

```bash
# 1) make sure working tree is clean and on main
git status
git checkout main
git pull

# 2) build the distribution
make clean && make dist
# verify DLL check is clean (see above)

# 3) tag and push
git tag -a v0.1.N -m "v0.1.N — short summary"
git push origin v0.1.N

# 4) draft the release notes in a heredoc (see past releases for style)
# 5) create the GitHub release with the zip attached
"/c/Program Files/GitHub CLI/gh.exe" release create v0.1.N \
  --title "v0.1.N — short title" \
  --notes "$(cat <<'EOF'
...
EOF
)" \
  feedback-windows-x64.zip
```

Release notes template lives in the previous release — fetch with
`gh release view v0.1.0 --json body` if needed.

### If you want inline gallery images in the release

Assets in the `Assets` section of a release are downloadable files, not
inline thumbnails. For inline images (as we did in v0.1.0):

1. Create an orphan branch `release-gallery-vX.Y.Z`:

   ```bash
   git checkout --orphan release-gallery-vX.Y.Z
   git rm -rf . --quiet
   cp /path/to/image*.png .
   git add *.png
   git commit -m "Inline gallery for release vX.Y.Z"
   git push origin release-gallery-vX.Y.Z
   git checkout main
   ```

2. Reference the images in the release body via
   `https://raw.githubusercontent.com/<user>/<repo>/release-gallery-vX.Y.Z/<file>`.

Keeps the release Assets list clean while still showing inline images.

## Test (currently manual)

No automated test suite yet — this is a gap. See TODO.md.

What to verify after a change, in order:

1. **Builds clean.** `make clean && make` with no new warnings.
2. **Launches to picker.** `./feedback.exe` → picker appears, option 1
   reaches a render loop.
3. **Renders something.** Press `3` (dot pattern), tap `Space`. Visible
   pattern cascades into feedback dynamics.
4. **Layers respond.** Toggle F1-F10, Ins, PgDn. Each changes visual
   output as described in the HUD.
5. **Preset save/load round-trips.** Tweak params, `Ctrl+S`, check
   `presets/auto_*.ini` for the new timestamps and non-default values.
   Then `Ctrl+N` / `Ctrl+P` to cycle back. Confirm `[vfx]`, `[output]`,
   and `[bpm]` sections round-trip.
6. **Recording captures frames.** ``Ctrl+` `` (backtick) starts recording.
   Run for a few seconds, ``Ctrl+` `` again to stop. Count frames in
   `recordings/feedback_<ts>/`. Open one EXR in a viewer (Krita, Nuke,
   ffmpeg) to confirm valid data.
7. **Screenshot writes PNG.** `PrtSc`. Check `screenshots/shot_*.png`.
8. **Shutdown cleanly at 4K60.** Currently known-slow — see TODO.md.

**Since the `roland-v4` branch also verify:**

9. **Help panel opens top-left, no dim.** Press `H`. Panel is ~380×420
   at 720p, sits top-left, main feedback stays visible behind it.
10. **Help navigation.** Up/Down cursor moves section selector. Enter
    drills in. Esc backs out. Section body shows live values that
    update as you turn related parameters.
11. **Section legend.** Drill into a section (e.g. Color) — a short
    legend appears under the body showing the current gamepad map.
12. **Gamepad detected.** Plug in Xbox pad, launch app. Stdout line:
    `[gamepad] <name> connected — press Back (View) button or H to
    open help panel`. Bottom-right of window shows `Warp · Back: help`.
13. **Contextual gamepad.** Open help, drill into Color. Turn right
    stick — gamma/contrast move. Drill into Thermal. Same stick now
    moves speed/rise. No cross-section leakage.
14. **Invert visible.** Press `V` (or drill Physics section, press Y
    on gamepad). Colors invert immediately even with Physics layer
    toggled off.
15. **V-4 effects cycle.** Drill VFX-1 section. `Alt+]` or gamepad
    RB cycles through 19 entries (0 off + 18 effects). Confirm each
    renders — Strobe pulses, Mirror reflects, Multi tiles, PinP
    shows an inset.
16. **BPM tap.** Drill BPM. Tap `Tab` (or gamepad A) four times in
    rhythm — BPM number updates. `Ctrl+Tab` turns sync on; visual
    modulations (inject, flash, decay dip) fire on beats.
17. **Output fade.** Drill Output. `Ctrl+Up` / `Ctrl+Down` or right
    stick Y fades to white / black. Stick is absolute (self-centers
    to 0).
18. **Bindings.ini generated.** First run writes `bindings.ini` next
    to the exe. Edit a keyboard binding (e.g. change `warp.zoom+ = Q`
    to `warp.zoom+ = Z`), restart, confirm new binding works.

**Since the `music` branch (v0.1.3) also verify:**

19. **Audio output.** Launch prints `[audio] … @ 48000 Hz, 24 voice
    pool, fx online`. Default `01_breakbeat.strudel` preset plays
    audibly within a second — kick on beat 1, snare on beat 3, 8
    hats, saw bass.
20. **Pattern engine loaded.** Stdout shows `[music] QuickJS 0.14.0
    runtime up` and `[js] [engine] pattern engine v0.1 loaded`.
21. **Preset cycling.** `Ctrl+Alt+N` / `Ctrl+Alt+P` steps through the
    5 music presets + `00_metronome`. HUD toast shows the name.
22. **Hot-reload.** Edit `music/01_breakbeat.strudel` externally
    (change a number), save. Stdout prints `[music] hot-reloaded
    '01_breakbeat'` within ~250 ms; audio reflects the edit.
23. **Momentary breakbeat.** Hold `Space` — music jumps to
    `01_breakbeat`, releases back to previous preset on key-up.
    Visual inject also fires (existing behaviour).
24. **Scheduler sanity.** Cycle to `00_metronome`. Should sound
    steady: boom — tap — boom — tap at 120 BPM. If it's
    irregular or bursty, the scheduler regressed (see ADR-0013).
25. **Virtual MIDI port.** Log line `[midi] teVirtualMIDI port
    'feedback' created — visible to Strudel et al`. Open any MIDI
    monitor (MidiView, MIDI-OX) — `feedback` appears as an output
    device.
26. **Strudel sync.** Open strudel.cc, run:
    ```
    stack(
      s("bd sn").midi("feedback"),
      midicmd("clock*24,<start stop>/2").midi("feedback"),
    )
    ```
    Hit play. Drill H → BPM. Should flip from `no clock (tap
    active)` to `LOCKED` with the tempo Strudel is running.
27. **fb.X scalars.** Drill H → Music. Move zoom (A/Q) and rotation
    (W/S) — the `fb.zoom`, `fb.theta` values in the panel update
    every frame; audible chord direction / filter sweep in the
    current preset changes with them.

Adding an automated smoke test (headless launch, render one frame,
exit) is a P0 item.

## Troubleshooting

### Build fails: `make: g++: No such file or directory`

You're in MSYS2 but not the MINGW64 shell. Open the "MSYS2 MINGW64"
terminal (blue icon), not "MSYS2 MSYS" (purple) or UCRT64 (yellow).

### Build fails: `Cannot create temporary file in C:\WINDOWS\`

Your `TMP` points to somewhere unwritable. From the shell:
`export TMP=$HOME/tmp && mkdir -p $HOME/tmp`.

### `make dist` fails: `zip: No such file or directory`

We use PowerShell's `Compress-Archive` rather than the `zip` tool, so
this shouldn't happen post-v0.1.0. If you see it, check the Makefile's
`dist` target — the `powershell.exe -NoProfile -Command Compress-Archive`
line should be there.

### Binary runs on dev box but crashes on clean Windows

DLL still dynamically linked. Run `objdump -p feedback.exe | grep "DLL Name:"`
and check for `libwinpthread-1.dll`, `libstdc++-6.dll`, `zlib1.dll`,
`glfw3.dll`, `glew32.dll`. The Makefile's `-Wl,-B*` ordering is
delicate. See ADR-0003.

### Recorder drops every frame

Encoders falling behind. Try `--rec-uncompressed` (much faster writes)
or raise `--rec-encoders`. `--rec-ram-gb` increase helps absorb bursts.

### App seems to hang on exit after recording at 4K

Not a hang — encoders draining the RAM buffer. At 4K60 with ~100
frames queued this can take 15-30 seconds. See TODO.md for the fix.

### Shaders don't hot-reload

`\` (backslash) key. If you hit it and nothing happens, check the
console for a GLSL compile error. The old program keeps running on
compile failure.

### No camera detected on startup

Log line: `[camera] couldn't negotiate NV12, YUY2, or RGB24`. Expected
if no camera attached or the camera only offers MJPG. The `external`
layer becomes a no-op; app still runs. Not a bug.

### No audio

Check stdout for `[audio] … fx online`. If the audio block is missing
the app failed to open the default output device.

- **Wrong device selected** in Windows audio output settings. miniaudio
  opens the default. Switch the default device, relaunch.
- **Exclusive-mode hold** — another app (some DAWs, Zoom under certain
  configs) grabbed the device in WASAPI exclusive mode. Close that
  app or change its audio output.
- **`[audio] ma_device_init failed`** — miniaudio couldn't open any
  backend. Very rare on Windows; try `--no-audio` if we add one. For
  now, the app continues running silently.

### Music pattern sounds random / bursty

- After a long alt-tab: recent versions clamp dt to 100 ms per frame,
  so this should not happen. If it does, scheduler dt coupling
  regressed — see ADR-0013.
- `FEEDBACK_MUSIC_DEBUG=1 ./feedback.exe` prints one `[sched]` line
  per triggered event with cycle time and delay. If the same
  `wbeg=0.xyz` fires multiple times, the `hap.whole.begin` dedupe
  broke — see ARCHITECTURE invariant #9.

### Strudel can't see the `feedback` MIDI port

- Driver not installed: `Ctrl+M` in-app runs `winget install
  TobiasErichsen.loopMIDI`. UAC prompt must be accepted. Log line
  `[midi] teVirtualMIDI port 'feedback' created` confirms success.
- Driver installed but port still absent: reboot once (the driver
  registration sometimes needs a restart on first install).
- Strudel running in a browser that blocks Web MIDI (some Firefox
  configs): use Chrome/Edge, or go to `about:config` and enable
  `dom.webmidi.enabled`. Strudel needs to have been granted MIDI
  permission for the origin.

### Music preset plays but no sound matches the name

- `.strudel` file may reference samples that don't exist. Unknown
  names auto-synthesize if they match `bd/sn/hh/oh/cp/rim/cb`; others
  produce silence. Drop WAVs into `samples/` with the name referenced.
- Preset uses a combinator we haven't implemented yet. Check stdout
  for `[engine] '.methodName()' not yet implemented — skipped`. See
  ADR-0012 and the `_UNIMPL_METHODS` list in `js/engine.js`.
- Pattern uses `.range(lo, hi)`, `.add`, `.sub`, or `.zoom` — these
  deliberately error rather than no-op (ADR-0012). Look for a
  `[js] exception` message.

### Tried `--preset NAME` but values seem unchanged

The preset probably loaded correctly — it's just that some `auto_*.ini`
presets are saved right after boot with default values, so they're
indistinguishable from defaults. Inspect the file: if it matches
default values (e.g. `zoom = 1.010000`), it was a no-op save. Use the
curated `01_*` through `05_*` presets for verifiable differences.

### Gamepad not detected

Expected stdout line on launch: `[gamepad] <name> connected — press
Back (View) button…`. If missing:

1. Plug the pad in BEFORE launch (GLFW enumerates joysticks at
   startup; late-plugged pads are picked up on next `glfwPollEvents`
   so it should still work, but cold-plug is most reliable).
2. Test the pad with Windows' built-in "Set up USB game controllers"
   dialog. If Windows doesn't see it, the app won't either.
3. Non-Xbox pads need the SDL2 mapping. GLFW bundles recent SDL
   gamepad mappings; exotic pads may need a custom mapping string.
   See GLFW docs if this trips someone.

### V-4 effect does nothing visible

Every effect is collapsed from 5–10 V-4 variants into one effect
with a continuous CONTROL parameter. If the effect appears silent:

- Check the parameter isn't at a no-op boundary (e.g., Negative at
  param=0 is just the original image; PinP at param≈0.15 is a tiny
  corner inset you may have missed).
- Key/PinP families need a B-source; if no camera is connected and
  the slot's B-source is "camera", you see black. Cycle B-source to
  "self-reprocessed" (Y on gamepad in the VFX section, or
  `ACT_VFX*_BSRC_CYCLE` via keyboard chord).

### BPM sync not following

`[bpm]` section of help shows live state. Troubleshoot in order:

1. `sync = on`? Press `Ctrl+Tab` (or gamepad Y in BPM section).
2. At least one modulation toggle enabled? Default is
   inject-on-beat + strobe-rate lock. If you toggled them all off
   visually nothing happens even when beats fire.
3. Tap-tempo taken? Tap `Tab` 3-4 times in rhythm; BPM number should
   converge.
4. External MIDI Clock is NOT wired yet (see TODO.md "Real MIDI
   input"). Until that lands, tap-tempo is the only clock source.

### bindings.ini says "unknown action 'xxx' — skipped"

You (or an older version of the app) wrote an action name that the
current binary doesn't recognise. Usually harmless — delete the line
or rename to a known action. `ActionId` catalogue is in
`input.cpp::ACTIONS[]`; each entry has its public `name` field.

## Bisecting a regression

The repo is small enough that `git bisect` is usually overkill — manual
checkout of recent commits is faster. Key landmarks:

- `v0.1.0` tag — first portable release. Baseline for "did it work."
- Major commits listed in `git log --oneline`.

If a visual regression appears, the likeliest culprits are
`shaders/layers/*.glsl` changes or the dispatch order in
`shaders/main.frag`.

## Repo hygiene

- `.o` files, `feedback.exe`, `recordings/`, `screenshots/`,
  `feedback-windows-x64/`, `feedback-windows-x64.zip` are all
  gitignored.
- `bindings.ini` is auto-written on first run. Users will hand-edit it.
  If you change a default, ship the new default value in
  `Input::installDefaults` — don't expect them to delete the file.
- `research/` PDFs are large (~10 MB) and tracked. Don't add more
  binary assets there without discussion.
- `gallery/` holds README screenshots, also tracked. Keep to ~4 files.
- `WORKLOG.md` at root is session-specific; not canonical. Can be
  cleared or archived without fear.
- `development/plans/` holds forward-looking planning docs for work
  queued but not started. Move into an ADR + update TODO.md when the
  work is picked up.
