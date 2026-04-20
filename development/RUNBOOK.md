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

## Build

```bash
make                   # incremental
make clean && make     # full rebuild
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
   Then `Ctrl+N` / `Ctrl+P` to cycle back.
6. **Recording captures frames.** ``Ctrl+` `` (backtick) starts recording.
   Run for a few seconds, ``Ctrl+` `` again to stop. Count frames in
   `recordings/feedback_<ts>/`. Open one EXR in a viewer (Krita, Nuke,
   ffmpeg) to confirm valid data.
7. **Screenshot writes PNG.** `PrtSc`. Check `screenshots/shot_*.png`.
8. **Shutdown cleanly at 4K60.** Currently known-slow — see TODO.md.

Adding an automated smoke test (headless launch, render one frame,
exit) is a P1 item.

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

### Tried `--preset NAME` but values seem unchanged

The preset probably loaded correctly — it's just that some `auto_*.ini`
presets are saved right after boot with default values, so they're
indistinguishable from defaults. Inspect the file: if it matches
default values (e.g. `zoom = 1.010000`), it was a no-op save. Use the
curated `01_*` through `05_*` presets for verifiable differences.

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
- `research/` PDFs are large (~10 MB) and tracked. Don't add more
  binary assets there without discussion.
- `gallery/` holds README screenshots, also tracked. Keep to ~4 files.
- `WORKLOG.md` at root is session-specific; not canonical. Can be
  cleared or archived without fear.
