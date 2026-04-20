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

**Homebrew on Apple Silicon macOS:**

```bash
brew install glfw glew pkg-config
```

**Optional for releases:**

- GitHub CLI (`gh`) authenticated to the repo. Check with `gh auth status`.
- `pdftotext` from `mingw-w64-x86_64-poppler` if you need to quote
  research papers.

## Build

```bash
make                   # incremental
make clean && make     # full rebuild
```

**Apple Silicon macOS:**

```bash
make -f Makefile.macos                   # incremental
make -f Makefile.macos clean && make -f Makefile.macos
```

Expected output includes one warning from `stb_easy_font.h` about an
unused function — harmless. Any other warning is worth investigating.

## Run

```bash
./feedback.exe                      # picker shown (argc == 1)
./feedback.exe --fullscreen         # picker skipped
./feedback.exe --demo               # gallery mode
./feedback.exe --help               # flag reference
```

**Apple Silicon macOS:**

```bash
./feedback --fullscreen
./feedback --demo
./feedback --help
```

See README.md for the full flag cookbook.

## Package (make dist)

Produces `feedback-windows-x64.zip` — the distribution artifact.

```bash
make clean && make dist
```

This runs:
1. Clean + rebuild.
2. Stage `feedback.exe + shaders/ + presets/ + README + LICENSE + CREDITS`
   into `feedback-windows-x64/`.
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

## Package on macOS (`make -f Makefile.macos dist`)

Produces `feedback-macos-arm64.zip`.

```bash
make -f Makefile.macos clean && make -f Makefile.macos dist
```

This runs:
1. Clean + rebuild via the macOS makefile.
2. Stage `feedback + shaders/ + presets/ + README + LICENSE + CREDITS`
   into `feedback-macos-arm64/`.
3. Zip the directory with `ditto`.

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

### macOS release note

If the release includes the native macOS path, mention these explicitly:

- Homebrew runtime dependencies (`glfw`, `glew`)
- Apple OpenGL 4.1 ceiling
- camera permission requirement on first launch

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
    On macOS, also confirm the Command-key aliases still work even if
    `bindings.ini` was generated by an older build.

**Also verify on Apple Silicon when touching the macOS path:**

19. **Native build succeeds.** `make -f Makefile.macos clean && make -f Makefile.macos`.
20. **GL context comes up at Apple limits.** Startup log includes
    `GL 4.1` and `GLSL 4.10`.
21. **Camera path reaches AVFoundation.** On a fresh machine, startup
    either negotiates a camera frame size or clearly reports that camera
    permission was denied.
22. **Package builds.** `make -f Makefile.macos dist` creates
    `feedback-macos-arm64.zip`.

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

### macOS camera denied

Log line: `[camera] access denied by macOS privacy settings` or
`[camera] access request denied by user`.

Fix:

1. Open `System Settings -> Privacy & Security -> Camera`.
2. Re-enable camera access for the app or terminal launching `./feedback`.
3. Relaunch the binary.

If permission behavior is flaky, remember this is currently a bare
binary, not a signed `.app` bundle.

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
  `feedback-windows-x64/`, `feedback-windows-x64.zip`,
  `feedback-macos-arm64/`, `feedback-macos-arm64.zip` are all
  gitignored.
- `bindings.ini` is auto-written on first run. Users will hand-edit it.
  If you change a default, ship the new default value in
  `Input::installDefaults` — don't expect them to delete the file.
  macOS is the one exception where startup also backfills missing
  Command-key aliases so old configs stay usable on Apple keyboards.
- `research/` PDFs are large (~10 MB) and tracked. Don't add more
  binary assets there without discussion.
- `gallery/` holds README screenshots, also tracked. Keep to ~4 files.
- `WORKLOG.md` at root is session-specific; not canonical. Can be
  cleared or archived without fear.
- `development/plans/` holds forward-looking planning docs for work
  queued but not started. Move into an ADR + update TODO.md when the
  work is picked up.
