# macOS / Apple Silicon

This subtree keeps the experimental macOS build isolated from the
Windows-first app at the repo root.

## Prereqs

```bash
brew install glfw glew pkg-config
```

## Build

```bash
cd macOS
make
```

This produces `macOS/feedback.app`, a Finder-launchable app bundle.

## Package

```bash
cd macOS
make dist
```

This produces `macOS/feedback-macos-arm64.zip`.

## Launch

Double-click `feedback.app` in Finder, or:

```bash
cd macOS
open feedback.app
```

The app bundle embeds:

- `shaders/` and `presets/` in `Contents/Resources`
- `libglfw.3.dylib` and `libGLEW.2.3.dylib` in `Contents/Frameworks`

At runtime the app copies the bundled starter presets into:

`~/Library/Application Support/Crutchfield Machine/presets`

It also writes `bindings.ini`, screenshots, and recordings under the same
Application Support folder, so Finder launches do not depend on the shell
working directory.

## Camera

- On first launch, macOS should ask for camera permission.
- If access was denied, re-enable it for `feedback` under
  System Settings -> Privacy & Security -> Camera.

## MIDI / DDJ-FLX2

The macOS build includes CoreMIDI input plus simple pad LED feedback. It
defaults to the DDJ-FLX2 map documented in `MIDI_NOTES.md`, and writes
editable MIDI bindings into `bindings.ini`.

Use `--midi-learn` to print incoming notes and CCs while touching controls.

## Notes

- This is still an experimental Apple Silicon path.
- A downloaded release zip will still need normal macOS signing/notarization
  work if you want Gatekeeper to trust double-click launches on other
  machines without manual override.
