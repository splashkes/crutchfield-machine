# macOS / Apple Silicon status

The runnable macOS path now lives at the repo root:

```bash
brew install glfw glew pkg-config
make -f Makefile.macos
./feedback --fullscreen
```

What works today:

- Native `arm64` build on Apple Silicon.
- GLFW + GLEW + Apple's OpenGL 4.1 core profile.
- Shared renderer, presets, screenshots, overlay, and EXR recorder code.
- AVFoundation webcam capture for the `external` layer.

Camera note:

- On first launch, macOS should ask for camera permission.
- If access was denied, re-enable it for the app launching `./feedback`
  under System Settings -> Privacy & Security -> Camera.

This directory remains notes/reference material for a deeper native macOS
port. The root app is now the practical starting point for getting the
instrument running on Apple Silicon.
