# Contribution notes · sean-evans/dynamics-and-controls

Date: 2026-05-30
Author: Sean Thomas Evans (@seanthomasevans)
Base: `splashkes/crutchfield-machine` · `main` at `07b5f4e`
Diff: 35 commits, +15691 / −140 lines across 74 files (Syphon + Link
submodules account for most of the line count)

This branch turns the engine into a real instrument over the Crutchfield
1984 model. Two new input/output features ship alongside (Continuity
Camera, hardware HEVC recorder), plus a paper-grounding pass that
deduplicates drifted classifier logic, fixes a submodule blocker, and
brings docs into agreement with code.

## How to read this branch

Commits are atomic. Each one is self-contained, has a written reason,
and can be cherry-picked in isolation unless explicitly noted. Read this
file plus the per-commit messages for the full story.

For a reviewer who only wants the headline · jump to the **Themes** map
below. The PR body on GitHub mirrors this file.

## Themes

The 35 commits group into nine themes. Each theme is roughly one
coherent contribution. Cherry-pick at theme granularity or commit
granularity.

### Theme 1 · OSC ingestion and instrumentation (4 commits)

Foundation for everything else. UDP OSC listener, dispatch through the
existing action handler, Launch Control v1 + TouchDesigner integration,
cross-platform build wiring. Predecessor commits to upstream's `osc`
branch.

```
e51d293 osc-control: scoped implementation plan
4ce476c Phase 1: UDP OSC ingestion dispatched through action handler
f8c02cf Phase 2+4: Launch Control + TouchDesigner integration
2674f31 LC v1 bindings + cross-platform build wiring + exit crash fix
```

### Theme 2 · Initial doc set under docs/ (1 commit)

Covers the new OSC plumbing.

```
c76a90c osc-control: comprehensive documentation set under docs/
```

### Theme 3 · "PR1-PR9" feature pack (9 commits)

Each commit is a single self-contained PR-sized feature. Originally
planned to land as nine separate PRs; combined on this branch because
each builds modestly on the OSC foundation in Theme 1.

```
f8974e4 PR1: hot reload of bindings.ini (mtime polling + SIGHUP)
363e3f6 PR2: OSC echo (bidirectional) — every dispatch emits /cma/echo/<action>
28ac168 PR3: action macros + state snapshots
dab8e84 PR4: OSC address patterns + OSC bundle timetag scheduling
32b5e9f PR5: built-in audio reactivity — RMS / peak / 3-band envelope
b1dea30 PR6: Mathlab dashboard — elegant analytical overlay (read-only billboard)
0fce163 PR7: GitHub Actions CI + Homebrew formula + Vitepress docs site
c8d8398 PR8: Ableton Link integration — network tempo sync
dd329da PR9: Syphon output — publish feedback texture to Resolume / TD / MadMapper / OBS
```

Cherry-pickable as a unit or individually. PR1 / PR2 / PR3 / PR8 / PR9
each carry their own `docs/features/*.md`.

### Theme 4 · Cross-platform + minor fixes (1 commit)

```
4122250 cross-platform fixes + functional issues
```

### Theme 5 · Mathlab evolution into an interactive cockpit (8 commits)

The biggest creative thread. The Mathlab dashboard from PR6 (Theme 3)
started as a read-only analytical billboard. Over these eight commits
it became the DYNAMICS cockpit · clickable, drag-editable, snapshot-aware,
visually grounded. Last commit renames Mathlab → DYNAMICS to settle on
language that matches the paper.

```
add6e32 docs: full coverage of all features added on osc-control
7707c43 --no-music / --music CLI flag + launch.sh defaults to silent
f0f8fdf UX fixes — silent by default + Mathlab made readable
03ae60f Mathlab: interactive parameter editor — arrow keys nav + adjust
d41d949 Mathlab: proper TTF text rendering (Inter) + dedicated nav actions
1b0992b Mathlab: real sliders per row + mouse drag direct-edit
e9dd910 Math-derived meta-controls: the actual instrument evolution
bf5e86b Mathlab: strip duplicate editor, become pure analytical view
ad85cf9 ui_panel: regime badge in the dock — analytical state always visible
39671fd ui_panel: bifurcation markers on couple and decay sliders
4f23088 docs: meta-controls deep dive + Mathlab role reframe + research brief
c80cba2 Mathlab → DYNAMICS: interactive cockpit, not a billboard
```

Cherry-pickable as a group. Internal sequencing matters · skip a middle
commit and the later ones may not apply cleanly.

### Theme 6 · Crash fix · launch.sh stale-build detection (1 commit)

```
1a361e6 crash fix: stale-build detection in launch.sh
```

Mixed-ODR builds were SIGSEGVing when struct layout changed across
TUs. Force-clean rebuild when overlay.h / input.h / ui_panel.h /
text_render.h / osc.h / link_glue.h / syphon_glue.h is newer than the
binary.

### Theme 7 · DYNAMICS cockpit production-ready (1 commit)

```
40f5880 DYNAMICS cockpit is finally clickable
```

Two bugs were stacked, blocking every mouse press from reaching the
cockpit. `UiPanel::mouseButton` returned `true` unconditionally while
the dock was visible (swallowed every click). Cursor coords weren't
being Retina-scaled before the overlay's framebuffer-pixel hit-test.
Both fixed; cockpit drag and click works end to end.

### Theme 8 · New features · independent (2 commits)

Either can land alone. No dependency between them or on the cockpit
work above.

```
c5e9700 camera: Continuity Camera + device picker (--camera / --list-cameras)
e38eed8 HQ MP4 recorder via hardware HEVC (popen → ffmpeg)
```

**Continuity Camera** · adds `AVCaptureDeviceTypeContinuityCamera` +
`External` + `DeskViewCamera` to AVFoundation discovery. iPhone shows
up alongside FaceTime HD and OBS Virtual. `--camera <substring>`
picks by name. `--list-cameras` enumerates everything AVF sees.

**HQ MP4 recorder** · second recorder alongside the EXR archive path.
Hardware HEVC via `hevc_videotoolbox`, piped to ffmpeg over `popen`.
Three codec presets cycle (HEVC / H264 / ProRes). Captures from the
pre-overlay sim FBO so HUD never bakes in.

### Theme 9 · Paper grounding + safe refactors (5 commits, this PR's primary contribution)

Wraps the rest of the work in the language of the foundational paper
(Crutchfield, "Space-time dynamics in video feedback", Physica D 10
(1984) 229–245), fixes a submodule blocker that prevented fresh clones
from building, dedupes drifted classifier logic, and corrects regime
preset behaviour that didn't actually land in the named regime.

```
ea1132d ground the engine in Crutchfield 1984 + fix submodule blocker + dedupe classifier
2f1125e cockpit: real two-row snapshot UI · save and recall both labeled, occupancy + regime tint visible
0ed59d5 phase B · regime presets land in the gate they name, cockpit gains paper-symbol readouts, toggles honour magnitude
3086f2a phase C · extract bind_feedback_params · dedupe planar + volumetric render paths
```

Each carries its own detailed commit message · what / why / verified.
Each cherry-picks cleanly on a vanilla `main`. See per-commit messages
for the full story.

## Smoke-test status at branch tip

Verified on macOS 14+, Apple Silicon, with the iPhone-Continuity rig.
| Feature | Status |
| --- | --- |
| Build (`make -f Makefile.macos clean && make -j8`) | PASS |
| Crutchfield runtime (`./launch.sh`) | PASS |
| Continuity Camera (`./launch.sh --camera phone`) | PASS · 1080p BGRA |
| Continuity discovery (`./feedback --list-cameras`) | PASS · iPhone + Desk View + FaceTime + OBS |
| MP4 recorder (Cmd+R or OSC `/cma/rec/mp4`) | PASS · valid HEVC via ffprobe |
| EXR recorder (backtick or OSC `/cma/rec/toggle`) | PASS |
| DYNAMICS cockpit (M) | PASS · all four regime presets land in the named gate first press |
| OSC echo (`/cma/echo/*` and `/cma/math/*`) | PASS · 30 Hz math broadcast |
| Snapshots (cockpit two-row UI · OSC `snapshot.save/recall`) | PASS · save and recall round-trip; regime-tagged tint |
| Ableton Link (`--link`) | PASS · listed in `[link] discovery enabled` |
| Syphon (`--syphon Crutchfield`) | PASS · `[syphon] publishing as 'Crutchfield'` |

Five audit reports under `research/audits/2026-05/` document the
verification methodology and the open follow-up items.

## Submodule + clone instructions

Fresh clone requires `--recurse-submodules` because `vendor/syphon` and
`vendor/link` are gitlinks (this PR fixes the missing `.gitmodules`
file that was a blocker upstream).

```bash
git clone --recurse-submodules https://github.com/splashkes/crutchfield-machine.git
cd crutchfield-machine
git checkout sean-evans/dynamics-and-controls
make -f Makefile.macos -j8
./feedback --list-cameras
./launch.sh --camera phone
```

## Selectively merging

Each theme is reviewable on its own. Recommended order if you want to
land subsets:

1. Theme 1 + 2 (OSC foundation + docs) · prerequisites for everything.
2. Theme 3 (PR1-PR9 feature pack) · independently merged.
3. Theme 4 (cross-platform fixes) · independently merged.
4. Theme 5 (Mathlab → DYNAMICS evolution) · as a group, sequenced.
5. Theme 6 (launch.sh crash fix) · independent.
6. Theme 7 (cockpit clickability) · depends on Theme 5.
7. Theme 8 (Continuity Camera, HQ MP4) · independent of cockpit; pure additions.
8. Theme 9 (paper grounding, snapshot UI, regime presets, refactor) · depends on Theme 5 + 7.

Individual commits can be `git cherry-pick`'d. Per-commit messages
explain what / why / verified for each.

## Open follow-ups not in this PR

Tracked in the audit reports under `research/audits/2026-05/`:

- **External → f + L' split** (paper-faithful camera-vs-feedback control).
  Defer because it changes a public OSC binding name. Discuss approach
  before doing.
- **Color crosstalk scalar → `mat3 L̄` matrix** (paper eq 5).
  Defer · bigger shader rewrite, more invasive than this PR's scope.
- **Temporal averaging FBO** (paper eq 4).
  Defer · new memory cost, needs design discussion.
- **Symmetry-lock / spiral / pinwheel auto-detection in the classifier
  (vs just readout)**. Cockpit now shows n-fold + spiral-pitch readouts
  but does not classify into named phenomenon regimes.
- **Replace scalar uColorCross with mat3 uLBar** (shader-audit R4).

The shader-audit R1-R7 and cockpit-audit refactors 1-12 are the
comprehensive list.

## Contact

Open an issue on `seanthomasevans/crutchfield-machine` for questions
about this branch, or reply on the PR thread.
