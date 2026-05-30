# MP4 recorder · hardware HEVC for share

A second recorder alongside the EXR archive path. EXR is for grade-from-source archival; this one is the "shareable in five seconds" output. Hardware-encoded via VideoToolbox on Apple Silicon. Pipes raw RGB frames into ffmpeg over a popen handle.

## Quick start

```bash
# Cmd+R toggles recording. Cmd+Shift+R cycles codec preset.
# Output lands in ~/Movies/CrutchfieldMachine/crutch-YYYY-MM-DD_HH-MM-SS.{mp4|mov}
```

Drive it from OSC:

```bash
# In bindings.ini [osc]:
rec.mp4.toggle = osc:/cma/rec/mp4
rec.mp4.codec  = osc:/cma/rec/mp4/codec
```

## Codecs

Cycled with `Cmd+Shift+R` or by firing `rec.mp4.codec`. The next time you press `Cmd+R` the new codec is used.

| Preset | Container | Codec call | Color | Use |
| --- | --- | --- | --- | --- |
| **HEVC** (default) | `.mp4` | `hevc_videotoolbox -q:v 65 -tag:v hvc1` | yuv420p | Share, web, iMessage |
| H264 | `.mp4` | `h264_videotoolbox -q:v 65 -tag:v avc1` | yuv420p | Maximum compatibility |
| ProRes 422 HQ | `.mov` | `prores_videotoolbox -profile:v 3` | yuv422p **10-bit** | Grade-grade archive |

## CLI flags

```
--mp4-dir DIR       output directory (default: ~/Movies/CrutchfieldMachine)
--mp4-quality 0-100 VideoToolbox quality knob (default: 65, ignored for ProRes)
--ffmpeg PATH       explicit ffmpeg binary (default: /opt/homebrew → /usr/local → PATH)
```

## What's captured

Frames are read from the same **sim-resolution pre-overlay FBO** that the EXR recorder uses. HUD and DYNAMICS cockpit overlays are drawn after the recorder grabs · they never bake into the output. Crop / aspect ratio matches `--sim-res` (default 1280×720).

## Architecture

`mp4_recorder.h` / `mp4_recorder.cpp`. Spawns ffmpeg as a subprocess via `popen` with raw RGB on stdin. Three-PBO async readback ring masks the `glReadPixels` fence. The oldest PBO is mapped each frame and `fwritten` to the pipe. No background thread in the host process · encoding runs in another process so its CPU/GPU cost doesn't compete with the feedback render loop.

On stop, `pclose` drains stdin and waits for ffmpeg to write the moov atom. Skipping this leaves a truncated file QuickTime refuses to open. The exit path stops any active recorder so the file always finalizes on quit.

## Smoke test

```bash
# Start recording, wait, stop.
python3 -c "
import socket
def pad(b): return b + b'\x00' * ((4 - (len(b) % 4)) % 4)
def s(p):  return pad(p.encode() + b'\x00')
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(s('/cma/rec/mp4') + s(','), ('127.0.0.1', 7700))
"
sleep 3
python3 -c "
import socket
def pad(b): return b + b'\x00' * ((4 - (len(b) % 4)) % 4)
def s(p):  return pad(p.encode() + b'\x00')
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(s('/cma/rec/mp4') + s(','), ('127.0.0.1', 7700))
"

ffprobe -v error -show_format ~/Movies/CrutchfieldMachine/crutch-*.mp4 | tail -5
```

Expect a valid HEVC file with duration > 0.

## Related

- [DYNAMICS.md](DYNAMICS.md) · cockpit
- [CAMERA.md](CAMERA.md) · iPhone Continuity input
- [OSC_ECHO.md](OSC_ECHO.md) · state observation while recording
