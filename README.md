# Video feedback — Windows build (RTX 3090)

Same dynamical system as the Linux/Mac versions. 12 toggleable layers, each
in its own .glsl file under `shaders/layers/`.

Camera is via Media Foundation (not V4L2 like the Linux version).

## Two ways to build

### Option A: MSYS2 / MinGW (fastest, recommended)

1. Install MSYS2 from https://www.msys2.org/
2. Open the "MSYS2 MINGW64" shell (the blue one, not the UCRT or CLANG ones).
3. Install deps:

       pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw \
                          mingw-w64-x86_64-glew make

4. `cd` to this folder and build:

       make

5. Run:

       ./feedback.exe

### Option B: Visual Studio 2022 + vcpkg

1. Install Visual Studio 2022 with the "Desktop development with C++" workload.
2. Install vcpkg (one-time):

       git clone https://github.com/microsoft/vcpkg C:\vcpkg
       C:\vcpkg\bootstrap-vcpkg.bat
       C:\vcpkg\vcpkg integrate install
       C:\vcpkg\vcpkg install glfw3:x64-windows glew:x64-windows

3. Open "x64 Native Tools Command Prompt for VS 2022" in this folder.
4. Run `build_msvc.bat`.
5. Run `feedback.exe`.

(Edit `VCPKG_ROOT` in build_msvc.bat if you installed vcpkg somewhere else.)

## Running

You can launch the exe either from the source folder (shaders resolve via
relative path `shaders/...`) or from anywhere — the app also looks for a
`shaders` folder next to the exe. So if you want a standalone distribution,
just copy `feedback.exe` and the `shaders/` directory into one folder.

## Controls

Press `H` at any time to open the in-window help panel (top-left, drill-down
by section). It lists every action with the current key binding and the live
parameter value — the panel stays visible while you play, so you can keep it
open while turning knobs.

Top-level sections: Status · Layers · Warp · Optics · Color · Dynamics ·
Physics · Thermal · Inject · VFX-1 · VFX-2 · Output · BPM · Quality · App ·
Bindings.

Hold `Shift` for 20× coarse steps on any parameter nudge.

### Remapping

Every action can be rebound via `bindings.ini` (written next to the exe on
first run). Actions are named like `warp.zoom+`, `preset.save`,
`bpm.strobeLock`. The same action can take keyboard, gamepad, and (future)
MIDI bindings — each source has its own `[section]`.

Xbox controller defaults: left stick translates, right stick rotates (X) +
output fade (Y, absolute), A = tap tempo, B = clear, X = pause, Y = help,
LB/RB = VFX-1 cycle, D-pad U/D = preset cycle, D-pad L/R = VFX-2 cycle,
Start = recording, Back = fullscreen, thumb clicks toggle external +
couple layers.

### V-4 slots

Two V-4-inspired effect slots sit at the tail of the pipeline. Each holds
one of 18 effects (Strobe, Still, Shake, Negative, Colorize, Monochrome,
Posterize, ColorPass, Mirror-H/V/HV, Multi-H/V/HV, W-LumiKey, B-LumiKey,
ChromaKey, PinP) with a single CONTROL parameter that varies its behaviour
across what the real V-4 spreads across 5–10 sub-variants. Key/PinP
effects take a "B source" (camera or the current frame itself) cycled
per-slot.

Default chords: `Alt+[` / `Alt+]` cycle slot 1; `Ctrl+Alt+[` / `Ctrl+Alt+]`
cycle slot 2. `Alt+;` / `Alt+'` adjust slot 1's parameter. `Alt+/` cycles
the B-source.

### BPM

`Tab` to tap tempo; `Ctrl+Tab` toggles beat sync. When sync is on, any
combination of these modulations fires on each beat:

- inject-on-beat (auto-press the inject trigger)
- strobe-rate lock (Strobe effect snaps to the beat)
- vfx auto-cycle (slot 1 cycles through effects)
- fade-flash (outFade pulses ±0.6 alternating, decays)
- decay-dip (decay drops to 0.90 for ~80 ms post-beat)

Beat division cycles x2 / x1 / ÷2 / ÷4 via `Alt+Tab`.

### Output fade

Bipolar dial: -1 = full black, +1 = full white. `Ctrl+Up` / `Ctrl+Dn`
nudge it by ±2%. Gamepad right-stick Y is an absolute-position binding
(self-centering), matching the feel of the V-4's Output Fade dial.

## First-run expectations

- On start the window will be solid black — that's correct. Nothing has been
  injected yet.
- Press `3` (dot pattern) and tap `Space` briefly. You should see a dot that
  immediately begins cascading through colour and geometric transforms.
- If the image vanishes instantly to black: `J` to lower decay (keep pressing
  until you hit ~0.998), then `C` to clear and try again.
- If it saturates to a solid colour: `Y` a few times (less contrast).
- `F9` enables the webcam layer — first launch will take a moment while
  Windows initialises Media Foundation. If there's no camera, startup prints
  a notice and the `F9` toggle becomes a no-op.
- `F8` enables Kaneko-style two-field coupling. Field A (displayed) starts
  sampling from Field B each frame, and B samples from A. Because the two
  fields run with mirrored rotation and hue rate, they don't trivially
  collapse into each other.
