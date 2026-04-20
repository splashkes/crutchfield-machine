# Contributing

This is a small project. PRs and issues are welcome. Nothing formal — read
this page, look at existing code, match the style.

## Ways to contribute

- **Add a new shader layer** (see below — the easiest entry point).
- **Author a preset** — save with `Ctrl+S` during a session, rename the
  `auto_*.ini` file to something descriptive, commit under `presets/`.
- **Port fixes for Linux/macOS** — see `linux/README.md` and
  `macOS/README.md` for current status.
- **Documentation** — screenshots, preset descriptions, teaching material.
- **Audio/MIDI/OSC integration** — real-time control is a natural fit
  for this system and is on the roadmap.

## Project layout

```
feedback-GBPS/
├── main.cpp             GLFW window, main loop, CLI, input handling
├── recorder.cpp/h       Lossless EXR recorder (reader + encoder threads)
├── overlay.cpp/h        HUD + help overlay (stb_easy_font)
├── camera.cpp/h         Windows Media Foundation webcam capture
├── exr_write.h          Self-contained EXR writer (half-float RGBA + ZIP)
├── shaders/
│   ├── main.vert/frag   Orchestrator — includes all layers, dispatches by bit
│   ├── blit.frag        Final copy-to-display pass
│   ├── common.glsl      Shared helpers (hsv, hash, etc.)
│   └── layers/*.glsl    One file per layer — the main extension point
├── presets/*.ini        Named parameter sets (01..05 are curated)
├── research/            Source papers + philosophy doc
├── linux/, macOS/       WIP ports (not feature-parity)
```

## Authoring a shader layer

Layers are the cleanest place to contribute. Each one lives in a single
`.glsl` file under `shaders/layers/` and runs as part of the per-pixel
feedback step. Existing layers are a 5-to-40 line template.

### Anatomy of a layer

Look at `shaders/layers/decay.glsl` for the simplest complete example:

```glsl
// layers/decay.glsl
// Exponential bleed to black.
//   uDecay : per-pass multiplier (1.0 = no decay; 0.99 = ~100-frame half-life)

vec4 decay_apply(vec4 c) {
    return vec4(c.rgb * uDecay, c.a);
}
```

Three things to notice:

1. **Top-of-file comment** describing what the layer does, in terms of the
   visual effect *and* the per-pass math. Parameters listed with their
   valid ranges. This is required — future-you needs it.
2. **One function, `<name>_apply(…)`** that takes and returns a `vec4`.
   Most layers take just a color. Some take `uv` too (warp, inject,
   external, couple, noise).
3. **Uniforms are declared in `main.frag`**, not in the layer file. The
   layer reads them by name.

### Steps to add a layer

Let's say you want to add a new `sharpen` layer.

1. Create `shaders/layers/sharpen.glsl` with your function:

   ```glsl
   // layers/sharpen.glsl
   // Unsharp-mask sharpen. Enhances edges by subtracting a blurred copy.
   //   uSharpen : 0.0 = off, 1.0 = full effect, >1 = crunchy
   vec4 sharpen_apply(vec4 c, sampler2D src, vec2 uv, vec2 res) {
       vec3 lo = 0.0.xxx;
       for (int dy = -1; dy <= 1; dy++)
         for (int dx = -1; dx <= 1; dx++)
           lo += texture(src, uv + vec2(dx, dy) / res).rgb;
       lo /= 9.0;
       return vec4(c.rgb + (c.rgb - lo) * uSharpen, c.a);
   }
   ```

2. `#include "layers/sharpen.glsl"` in `shaders/main.frag`.

3. Add a layer bit in `main.frag`:

   ```glsl
   const int L_SHARPEN = 1<<12;
   ```

   Bump any later bits if inserting in the middle.

4. Dispatch in the main function, at whatever stage makes sense (probably
   after `optics` and before `physics`):

   ```glsl
   if ((uEnable & L_SHARPEN) != 0)
       col = sharpen_apply(col, uPrev, uv, uRes);
   ```

5. Declare the uniform in `main.frag`:

   ```glsl
   uniform float uSharpen;
   ```

6. **Mirror the bit on the host side.** In `main.cpp`:
   - Add the same `L_SHARPEN = 1<<12` to the host enum.
   - Add a `LAYERS[]` entry with a key binding.
   - Add the `uSharpen` field in `Params` with a default.
   - Set the uniform via `glUniform1f` where the other layer uniforms are set.
   - Add the keyboard handler for adjusting it.
   - Add the HUD line in the help overlay.

The Ctrl+S preset system auto-saves any field in `Params`, so new params
get persisted for free — just add them to the load/save parser the same
way the existing fields are handled.

7. Reload shaders live with `\` to iterate without restarting.

### Style

- Explain the visual *and* the math in the top comment. A reader should
  be able to guess the right parameter range without running the code.
- Prefer short layer names (`warp`, `decay`, `noise`) — not
  `advanced_feedback_modulator`.
- No multi-line comment blocks inside layer bodies. If a step needs
  explaining, one short comment on the relevant line is enough.

## Contribution ideas

Shader layers we'd love PRs for:

- **Sharpen / unsharp mask** — outlined above.
- **Sobel / edge detect** — return edge magnitude as a color, useful as
  a modulator source for other layers.
- **Posterize / bit-crush** — quantize to N levels per channel (different
  from `--precision 8`, which quantizes the whole buffer).
- **Kaleidoscope / mirror folds** — N-way radial symmetry.
- **Displacement-map warp** — sample a noise texture to drive UV offsets,
  more organic than the existing zoom/rotation warp.
- **Bloom** — threshold-and-blur then additive blend back. Classic, nice.
- **Feedback-rate modulation** — per-pixel decay based on luminance
  (bright pixels decay faster → natural transient dynamics).
- **Glitch / scanline tearing** — driven by a time-varying noise source.
- **FFT-domain layer** — radial blur via DFT (would need a compute-shader
  path, more ambitious).

Host-side features:

- **Audio reactivity** — WASAPI loopback, extract RMS/bands, drive any
  parameter. See open TODO in main.cpp.
- **MIDI input** — use `rtmidi` or Windows MM; bind CCs to parameters.
- **OSC input** — `oscpack` or tiny hand-rolled UDP parser.
- **Preset menu overlay** — in-window browser for loading presets by
  keyboard (another contributor is looking at this).
- **`--record-on-start`** — start the EXR recorder immediately when the
  app launches (useful with `--demo` for unattended capture sessions).

Infrastructure:

- **GitHub Actions CI** that builds on Windows (MSYS2), Linux, macOS.
- **Release automation** that uploads `feedback-windows-x64.zip` on tag.
- **Screenshot / clip gallery** in the README.

If something in the list appeals, open an issue saying you're taking it
so we don't double-up.

## Submitting a PR

- Keep PRs focused — one layer, one feature, one fix.
- Describe the visual effect for shader PRs; a before/after screenshot
  or GIF is ideal.
- Match existing style: brief comments only when the *why* is
  non-obvious.
- Don't reformat unrelated code.

## Questions

Open a GitHub issue.
