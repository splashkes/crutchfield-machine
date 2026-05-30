# Syphon output — publish to Resolume, TouchDesigner, MadMapper, OBS

Crutchfield's rendered feedback can be published as a Syphon source on macOS. Any Syphon-aware app on the same Mac subscribes to the live texture without screen-capture, without any inter-process file round-trip.

Tested clients: Resolume Arena/Avenue, MadMapper, VDMX, TouchDesigner, OBS (via the Syphon plugin), AfterEffects/Premiere via Syphon Recorder. The protocol is IOSurface + a small Mach-message handshake; latency is essentially zero (one frame at most).

macOS only. No equivalent on Linux. Windows uses Spout — see "Windows / Spout" below.

## Quick start

```bash
# Default name "Crutchfield Machine"
./feedback --syphon

# Custom name (so multiple instances are distinguishable in TD's source picker)
./feedback --syphon "FOH-Display"
./feedback --syphon "Stage-Display"
```

In your Syphon client app:
- Resolume: Sources → Syphon → pick "Crutchfield Machine"
- TouchDesigner: drop a Syphon In TOP, Sender Name dropdown
- MadMapper: Input → Syphon → pick the source
- OBS: Source → Syphon Client (requires the Syphon plugin)

## What gets published

The simulation FBO texture at sim resolution, not the on-screen composite. The DYNAMICS panel, HUD, and help overlays are drawn into the default framebuffer (the window), not into the sim FBO. The publisher then hands the sim FBO texture to Syphon — overlays never touch it.

This matches the recorder policy: clean stream out, control surfaces stay local. Order in `main.cpp` is `S.ov.draw()` → `S.ui.draw()` → `syphon_publish()` → `glfwSwapBuffers()`, but the overlays render into the window not the sim FBO so the publish sees a clean image regardless. If you want the HUD in the published stream you'd need to redirect the overlay shaders to render into the sim FBO before the publish call, not just reorder.

## Configuration

CLI flag:

```
--syphon              publish as "Crutchfield Machine"
--syphon NAME         custom name
```

No `bindings.ini` config yet — Syphon naming is a launch-time choice. If you need runtime name changes, file an issue and we'll add an action.

## Build options

Syphon is **opt-in** in the macOS build to keep fresh-clone builds dependency-free.

```bash
# Build without Syphon (default, no framework needed)
make -f Makefile.macos                    # SYPHON=0 implied if vendor/syphon missing

# Build with Syphon (requires the Syphon.framework built)
make -f Makefile.macos SYPHON=1
```

When `SYPHON=0`, the `--syphon` flag is silently accepted but does nothing. `syphon_init()` is a stub that returns 0; `syphon_publish()` is a no-op.

### Building Syphon.framework

```bash
# Clone (already vendored on this branch)
git clone --depth 1 https://github.com/Syphon/Syphon-Framework.git vendor/syphon

# Build the framework via xcodebuild (requires Xcode + Metal toolchain)
cd vendor/syphon
xcodebuild -project Syphon.xcodeproj -target Syphon -configuration Release -arch arm64 build

# Output lands at vendor/syphon/build/Release/Syphon.framework
```

If `xcodebuild` complains about a missing Metal toolchain:

```bash
xcodebuild -downloadComponent MetalToolchain
# Takes a few minutes; one-time install
```

Then re-run the build.

### Bundling the framework with dist

`make -f Makefile.macos SYPHON=1 dist` copies `Syphon.framework` into `feedback-macos-arm64/Frameworks/`. The binary's rpath includes `@executable_path/Frameworks` so the bundled framework loads without extra setup.

End users running the published `feedback-macos-arm64.zip` get `--syphon` working out of the box — no need to install Syphon separately.

## How it works under the hood

`syphon_glue.mm` is Objective-C++ — it pulls in `<Syphon/Syphon.h>` and instantiates a `SyphonOpenGLServer` with the current `CGLContextObj`. Each frame, `syphon_publish()` calls `publishFrameTexture:textureTarget:imageRegion:textureDimensions:flipped:` to share the texture.

```objc
[g_server publishFrameTexture:tex_id
                textureTarget:tex_target
                  imageRegion:NSMakeRect(0, 0, w, h)
            textureDimensions:NSMakeSize(w, h)
                      flipped:NO];
```

IOSurface backing means zero-copy texture sharing across processes. Client apps get the same GPU texture; no memory transfer.

## Multi-instance gotcha

If you run two Crutchfields and both pass `--syphon` with the default name, only one will register (Syphon enforces unique names per session). Pass different `--syphon NAME` values to keep them distinct.

## Windows / Spout

Spout (<https://spout.zeal.co/>) is the Windows equivalent — same IOSurface-style GPU texture sharing, supported by Resolume, OBS, TouchDesigner, MadMapper. Crutchfield doesn't have Spout integration yet but it's straightforward: parallel to `syphon_glue.mm` but with the Spout SDK. PR welcome.

Until then, on Windows use NDI (network video) or screen capture as fallbacks for getting Crutchfield's output into other apps.

## Performance

Per-frame: one `publishFrameTexture:` call. Cost is dominated by an IOSurface flush which is a few microseconds. No GPU readback; no memory copy. Effectively free.

## Limitations

- macOS only (Spout on Windows is a future PR).
- Texture target is always `GL_TEXTURE_2D` — no cubemap or array publishing.
- Single sink. To publish to multiple Syphon servers, instantiate multiple `SyphonOpenGLServer`s — currently the glue is hard-coded to one.

## Implementation pointers

- `syphon_glue.h` — extern "C" interface (stubs when `CRUTCHFIELD_NO_SYPHON` is set or off-mac)
- `syphon_glue.mm` — Objective-C++ implementation with SyphonOpenGLServer
- `main.cpp`: `syphon_init()` after GL context init, `syphon_publish()` after `S.ov.draw()` but before `glfwSwapBuffers`
- `Makefile.macos`: SYPHON=0/1 conditional that gates the compile + link
- `vendor/syphon` — cloned `https://github.com/Syphon/Syphon-Framework.git`
