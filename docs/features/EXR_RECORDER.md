# EXR archive recorder · lossless float

The original recording path. Captures RGBA half-float frames directly from the float simulation FBO with no format conversion in the read path. Each recording is a directory of `frame_NNNNNN.exr` files plus `manifest.txt`. Use this when you need a master with grade headroom · scene-referred linear, full precision, no codec drift.

For a shareable file at the end of a session, see [MP4_RECORDER.md](MP4_RECORDER.md).

## Quick start

```
# Default keyboard binding: backtick (`)
```

Drive from OSC:

```ini
# In bindings.ini [osc]:
rec.toggle = osc:/cma/rec/toggle
```

```bash
# Toggle on, then off.
python3 -c "
import socket
def pad(b): return b + b'\x00' * ((4 - (len(b) % 4)) % 4)
def s(p):  return pad(p.encode() + b'\x00')
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(s('/cma/rec/toggle') + s(','), ('127.0.0.1', 7700))
"
```

## What's captured

Frames are pulled from the same pre-overlay sim FBO that the MP4 recorder uses. HUD and DYNAMICS panel never bake in. Resolution matches `--sim-res` (default 1280×720). Precision is half-float (16-bit per channel) by default; if the engine is running at `--precision 32` the readback writes float32 directly.

## CLI flags

```
--rec-fps N         recording framerate metadata tag (default: follows --fps)
--rec-ram-gb N      RAM buffer for the pool, GB (default: auto · min(free/4, 8 GB))
--rec-encoders N    encoder thread count (default: hw_concurrency - 2)
--rec-uncompressed  write uncompressed EXR · ~2× larger files, ~5-10× faster writes
```

## Pipeline

Three stages decoupled by queues:

1. **Render thread** · `capture(srcFBO)` enqueues `(pboIdx, fence)`. 8 PBOs cycle so the render thread never blocks on a readback.
2. **N readback workers** (4) · wait on fence, `glMapBuffer`, memcpy (with float32 → half conversion if needed) into a heap buffer from the RAM pool, unmap (frees PBO fast), enqueue on the encode queue.
3. **Encoder threads** (auto) · pure CPU, no GL context · `exr::write_rgba_half(...)` with optional compression, return buffer to pool.

The RAM pool is the main capacity knob. Buffer count = ramBudget / frameBytes.

## Output layout

```
recordings/
└── 2026-05-28_19-44-56/
    ├── manifest.txt
    ├── frame_000000.exr
    ├── frame_000001.exr
    └── ...
```

`manifest.txt` records sim resolution, fps, precision, codec setting, total frame count. Drop the directory into Resolve, Nuke, or your tool of choice and treat it as an image sequence.

## When to use EXR vs MP4

| You want | Use |
| --- | --- |
| A shareable file in seconds | **MP4 recorder** (Cmd+R) |
| A grade-grade master with headroom | **EXR recorder** (backtick) |
| Both | Fire both at once. Capture is from the same FBO, write paths don't conflict. |

## End-of-session ffmpeg encode

If a session produces several EXR runs, the launcher prints a per-recording reminder at exit listing the directories and a sample ffmpeg command to encode each to ProRes or HEVC.

## Implementation pointers

- `recorder.h` · `Recorder::Config`, `start`, `stop`, `capture`
- `recorder.cpp` · the three-stage pipeline, RAM pool, encoder threads
- `main.cpp` · `S.rec`, action `ACT_REC_TOGGLE`, capture call per frame next to the MP4 recorder call

## Related

- [MP4_RECORDER.md](MP4_RECORDER.md) · the share path
- [DYNAMICS.md](DYNAMICS.md) · cockpit
