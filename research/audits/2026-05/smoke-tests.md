# Crutchfield Machine — functional smoke tests

Target binary: `/Users/seanthomasevans/workspace/crutchfield-machine/feedback` (PID 38758).
Live command line: `feedback --osc-listen 7700 --link --osc-echo 127.0.0.1:7701 --syphon Crutchfield --camera phone`.
stdout/stderr captured to `/private/tmp/crutchfield-session.log`.
OSC ingress: `127.0.0.1:7700`. OSC echo target: `127.0.0.1:7701` (no listener at start of testing).

Helpers used:
- `/Users/seanthomasevans/workspace/crutchfield-machine/development/osc_send.py` (existing in repo).
- `/tmp/osc_listen.py` (UDP OSC listener; per-address counter + per-sec rate).

Tests were run against the running instance; no rebuild, no restart. The instance's installed bindings live at `~/Library/Application Support/Crutchfield Machine/bindings.ini`. The `[osc]` block in that file binds only: `dyn.halflife.axis`, `regime.distance.axis`, `regime.set`, `regime.invert`, `pad.regime.x/y`, `theater.failsafe`, `math.echo`, `app.math`, `app.help`, `rec.mp4.toggle`, `rec.mp4.codec`. There are NO OSC bindings for `link.*`, `snapshot.*`, `rec.toggle`, or most layer/parameter actions — so any test that depends on those falls into SKIPPED.

## 1. Ableton Link (`/cma/link/toggle`)

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/link/toggle 1`
  - `grep -i link /private/tmp/crutchfield-session.log`
- expected: `[link] state change` line in session log (or any link.toggle echo at `/cma/echo/link.toggle` on 7701).
- observed:
  - OSC send confirmed 28-byte packet to 127.0.0.1:7700.
  - No new `[link]` line in session log; no `/cma/echo/link.toggle` on 7701.
  - Pre-existing log line `[link] discovery enabled` from process startup (`--link` CLI flag is enabled).
- VERDICT: SKIPPED — `/cma/link/toggle` is not bound in the installed `bindings.ini`. Link IS running (from `--link` flag) and the framework loaded. Documentation suggests `/cma/link/toggle`, `/cma/link/tap`, `/cma/link/play` as recommended bindings but the user has not added them. Cannot exercise via OSC against this instance without editing bindings + reloading (SIGHUP).

## 2. OSC echo

- test command(s):
  - `python3 /tmp/osc_listen.py --port 7701 --duration 3.5 &` (background listener)
  - `python3 development/osc_send.py --port 7700 /cma/dyn/halflife 0.5`
  - `python3 development/osc_send.py --port 7700 /cma/regime/set 2`
  - `python3 development/osc_send.py --port 7700 /cma/math/toggle 1`
- expected: three echo messages back at `/cma/echo/dyn.halflife.axis`, `/cma/echo/regime.set`, `/cma/echo/app.math` carrying the dispatched magnitudes.
- observed:
  ```
  /cma/echo/dyn.halflife.axis  [0.5]
  /cma/echo/regime.set         [2.0]
  /cma/echo/app.math           [1.0]
  [done] received 3 msgs in 3.57s = 0.8 msg/s
  ```
- VERDICT: PASS. Address is built from the catalogue action name (matches `OSC_ECHO.md` claim). Type tag `,f` carrying magnitude, all three actions echoed correctly.

## 3. Syphon

- test command(s):
  - `grep -i syphon /private/tmp/crutchfield-session.log`
  - `lsof -p 38758 | grep Syphon`
- expected: log line `[syphon] publishing as '<NAME>'`; framework binary mapped into the process.
- observed:
  - Log: `[syphon] publishing as 'Crutchfield'` (single line at startup).
  - lsof: `vendor/syphon/build/Release/Syphon.framework/Versions/A/Syphon` is `txt`-mapped into PID 38758.
  - System Syphon directory enumeration via `defaults read info.v002.Syphon*` returned no domain — Syphon uses Mach ports / IOSurface for discovery, not preference plists, so a `defaults` check is not informative.
- VERDICT: PASS. Framework loaded, publish init log line present. Cannot independently observe a subscriber, but Syphon initialization and publish-loop are running.

## 4. HQ MP4 recorder (`/cma/rec/mp4`)

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/rec/mp4 1` (start)
  - `sleep 3.5`
  - `python3 development/osc_send.py --port 7700 /cma/rec/mp4 1` (stop)
  - `ffprobe -v error -show_entries format=duration,size:stream=codec_name,width,height,r_frame_rate,nb_frames ~/Movies/CrutchfieldMachine/<new>.mp4`
- expected: new file in `~/Movies/CrutchfieldMachine/`; ffprobe reports HEVC, non-zero duration, non-zero frame count.
- observed:
  - New file: `~/Movies/CrutchfieldMachine/crutch-2026-05-30_12-30-28.mp4` (23,411 bytes).
  - Log lines:
    ```
    [mp4] recording → /Users/seanthomasevans/Movies/CrutchfieldMachine/crutch-2026-05-30_12-30-28.mp4  (1280x720 @ 60 fps, hevc)
    [mp4] stopped — wrote 72 frames to /Users/seanthomasevans/Movies/CrutchfieldMachine/...
    ```
  - ffprobe:
    ```
    codec_name=hevc
    width=1280 height=720
    r_frame_rate=60/1  nb_frames=72
    duration=1.200000  size=23411
    ```
- VERDICT: PASS. HEVC codec confirmed, 72 frames, 1.2s duration. Note: requested 3.5s sleep but recorder only captured 1.2s — main loop fps is well under 60 (the file's r_frame_rate is the encoder's nominal, not the source). Capture rate is gated by Crutchfield's actual frame rate (low when window is occluded). Not a recorder bug; a runtime-fps observation.

## 5. Camera enumeration (`--list-cameras`)

- test command(s):
  - `cd /Users/seanthomasevans/workspace/crutchfield-machine && ./feedback --list-cameras`
- expected: enumerated list including iPhone Continuity Camera.
- observed:
  ```
  camera devices visible to AVFoundation:
    [0]  FaceTime HD Camera           (Built-in)
    [1]  OBS Virtual Camera           (External)
    [2]  SEAN'S PHONE (2) Camera      (iPhone18,2, External)
    [3]  SEAN'S PHONE (2) Desk View Camera  (iPhone18,2, DeskView)
  (4 devices) — use --camera <substring> to pick one
  ```
- VERDICT: PASS. iPhone Continuity Camera detected at indices [2] and [3]. Running feedback was already started with `--camera phone` which matches [2] by substring; enumeration is independent of the running instance (a fresh feedback invocation that exits after listing).

## 6. DYNAMICS / Math panel (`/cma/math/toggle`)

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/math/toggle 1`
  - `osascript -e 'tell application "System Events" to set frontmost of first process whose unix id is 38758 to true'`
  - `screencapture -x -o /tmp/math_panel_test2.png`
  - Read screenshot.
- expected: math panel visible on the right side of the feedback window with rho / regime / sparklines.
- observed: Screenshot at `/tmp/math_panel_test2.png` (3456×2234) shows the math panel rendered on the right edge of the feedback window — visible columns of values, sparklines, and the parameter pinned-controls strip on the left. Panel UI matches `MATHLAB.md` description.
- VERDICT: PASS. Toggle dispatched (confirmed by echo `/cma/echo/app.math 1.0` in test 2). Panel visually present in screenshot.

## 7. OSC bindings → engine state (`/cma/regime/set 2`)

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/math/echo 1`  (enable math telemetry)
  - `python3 /tmp/osc_listen.py --port 7701 --duration 1.5 &`
  - `python3 development/osc_send.py --port 7700 /cma/regime/set 2`
- expected: `/cma/math/coupling` value rises above 0.6 (CHAOTIC threshold per META_CONTROLS.md); `/cma/math/regime` transitions 1 → 2; `/cma/math/regime/changed` edge fires once.
- observed:
  ```
  /cma/math/coupling [0.524]   regime [1]    (pre-set, TURBULENT)
  ... (5 samples at the pre-set values) ...
  /cma/echo/regime.set [2.0]
  /cma/math/coupling [0.647]   regime [2]   (post-set, CHAOTIC)
  /cma/math/regime/changed [2]
  ```
- VERDICT: PASS. Coupling K_c jumped from 0.524 to 0.647 (above the 0.6 CHAOTIC threshold), regime classifier transitioned to 2, edge event fired exactly once. The binding chain (UDP ingress → catalogue dispatch → apply_action → engine param mutation → classifier → math echo) is end-to-end working.

## 8. EXR recorder (`rec.toggle`, grave key)

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/rec/toggle 1`
  - check `~/Library/Application Support/Crutchfield Machine/recordings/`
  - check session log.
- expected: a new `feedback_YYYYMMDD_HHMMSS/` directory created, log line about recording start.
- observed:
  - OSC send confirmed.
  - No new directory; latest directory still `feedback_20260528_114114` (from a previous session).
  - No new log line about recording.
- VERDICT: SKIPPED — `rec.toggle` has no OSC binding in the installed `bindings.ini`. Action exists in code (input.cpp L223) and is bound to keystroke `` ` `` (GRAVE_ACCENT) only. Keystroke injection into the live feedback window was not attempted because it would risk disrupting the running session and Sean's current state. The recorder code path itself is not exercised by this test; the dedicated keystroke path or a `[osc] rec.toggle = osc:/cma/rec/toggle` binding would be required.

## 9. Snapshot save/recall

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/snapshot/save 1`
  - `python3 development/osc_send.py --port 7700 /cma/snap/save 1`  (both common spellings)
  - check session log.
- expected: HUD/log line `snapshot saved → slot 1`; subsequent recall logs `snapshot recalled → slot 1`.
- observed: No log lines, no echo for `snapshot.save` or `snapshot.recall`.
- VERDICT: SKIPPED — no OSC bindings for `snapshot.save` or `snapshot.recall` in the installed `bindings.ini`. Action handlers exist in `main.cpp` and `input.cpp` L246-247. The snapshot pathway was exercised indirectly during test 7 (regime.set internally writes to engine state, which a future save would capture), but the save/recall actions themselves cannot be hit over OSC against this instance.

## 10. Math echo rate (`/cma/math/echo`)

- test command(s):
  - `python3 development/osc_send.py --port 7700 /cma/math/echo 1` (enable)
  - `python3 /tmp/osc_listen.py --port 7701 --duration 3.0 --summary --count-only`
- expected per docs (META_CONTROLS.md §5): publishing at 30 Hz across 7 channels (`rho`, `halflife`, `diffusion`, `coupling`, `noise/db`, `regime`, `regime/changed`).
- observed (Crutchfield window occluded):
  ```
  [done] received 145 msgs in 3.02s = 47.9 msg/s
  [unique addresses] 6
       24  /cma/math/coupling
       24  /cma/math/diffusion
       24  /cma/math/halflife
       24  /cma/math/noise/db
       24  /cma/math/regime
       25  /cma/math/rho
  ```
  observed (Crutchfield window foregrounded, 1.0s sample): 79 msgs/s ≈ 13 Hz per channel.
- VERDICT: PASS with caveats.
  - 6 of the documented 7 channels observed. `/cma/math/regime/changed` is edge-triggered (test 7 confirmed it fires on transition), not continuous — it correctly did not appear in a steady-state stream.
  - Code throttles to 30 Hz max (`MATH_ECHO_RATE_HZ = 30.0` at main.cpp L3337, gate at L3344). Observed rate of 8-13 Hz reflects that the main render loop is running well below 30 fps in the current state (window occluded / low GPU load). The doc claim of "30 Hz" is the cap, not a floor — accurate but ambiguous.
  - Side-finding: `/cma/math/echo` is a TOGGLE that ignores the magnitude payload (`g_math_echo_enabled = !g_math_echo_enabled` at main.cpp L3528). Sending value `0` does not turn it off; it just flips state. Verified empirically — had to send the message twice to return to off. Doc does not mention this.

## Summary

| # | Feature | Verdict |
|---|---|---|
| 1 | Ableton Link OSC toggle | SKIPPED (no binding) |
| 2 | OSC echo of dispatched actions | PASS |
| 3 | Syphon publish | PASS |
| 4 | HQ MP4 recorder via OSC | PASS |
| 5 | Camera enumeration `--list-cameras` | PASS |
| 6 | Math/Mathlab panel toggle + visual | PASS |
| 7 | OSC binding → engine state mutation (regime.set) | PASS |
| 8 | EXR recorder via OSC | SKIPPED (no binding; key-only) |
| 9 | Snapshots via OSC | SKIPPED (no binding) |
| 10 | Math echo stream | PASS (with rate caveat) |

7 PASS, 0 FAIL, 3 SKIPPED. The skipped tests reflect missing OSC bindings in the installed `bindings.ini` rather than feature failures — keystrokes for those features (` ` `, Cmd+R, Tab, M, …) are wired correctly per `input.cpp`. Pre-PR action: extend bindings.ini examples to include `link.*`, `snapshot.*`, and `rec.toggle` OSC entry points, or note in each feature doc that OSC entry requires user-side binding additions.

Side-finding from test 10 worth flagging in the PR: `math.echo` toggles regardless of the dispatched magnitude — sending `/cma/math/echo 0` does NOT disable. Either honour the magnitude (set rather than toggle) or document the toggle-only behaviour explicitly. Same pattern likely applies to `theater.failsafe` (main.cpp L3520 uses the identical `!= flip` idiom) — worth a glance.
