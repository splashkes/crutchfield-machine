# Crutchfield Machine — Documentation

In-depth docs for everything added on the `osc-control` branch: OSC control, MIDI controller integrations, audio reactivity, Ableton Link, Syphon output, action macros + state snapshots, hot-reload bindings, OSC echo, and the Mathlab dashboard.

## Start here

| If you want to… | Read |
| --- | --- |
| Drive Crutchfield from a Novation Launch Control v1 | [launch-control/V1_GUIDE.md](launch-control/V1_GUIDE.md) |
| Drive Crutchfield from a Launch Control XL | [launch-control/XL_GUIDE.md](launch-control/XL_GUIDE.md) |
| Drive Crutchfield from TouchDesigner | [touchdesigner/GETTING_STARTED.md](touchdesigner/GETTING_STARTED.md) |
| Understand the OSC layer | [osc/ARCHITECTURE.md](osc/ARCHITECTURE.md) |
| See every binding flag | [osc/BINDINGS.md](osc/BINDINGS.md) |
| Read raw OSC bytes | [osc/PROTOCOL.md](osc/PROTOCOL.md) |
| Look up CLI flags / config | [osc/CLI.md](osc/CLI.md) |
| Find a recipe | [osc/COOKBOOK.md](osc/COOKBOOK.md) |
| Browse every action | [osc/ACTIONS.md](osc/ACTIONS.md) |
| Diagnose something | [osc/TROUBLESHOOTING.md](osc/TROUBLESHOOTING.md) |

## New features (per-topic deep dives)

| Feature | Doc |
| --- | --- |
| Hot reload of `bindings.ini` | [features/HOT_RELOAD.md](features/HOT_RELOAD.md) |
| OSC echo (bidirectional) | [features/OSC_ECHO.md](features/OSC_ECHO.md) |
| Action macros + state snapshots | [features/MACROS_SNAPSHOTS.md](features/MACROS_SNAPSHOTS.md) |
| Audio reactivity (built-in) | [features/AUDIO_REACTIVITY.md](features/AUDIO_REACTIVITY.md) |
| Ableton Link | [features/ABLETON_LINK.md](features/ABLETON_LINK.md) |
| Syphon output | [features/SYPHON.md](features/SYPHON.md) |
| Mathlab dashboard (analytical view) | [features/MATHLAB.md](features/MATHLAB.md) |
| **Math-derived meta-controls** (halflife, regime.distance, compass, failsafe, math.echo) | [features/META_CONTROLS.md](features/META_CONTROLS.md) |

## What's new in `osc-control`

- **`osc.h` / `osc.cpp`** — minimal OSC 1.0 UDP listener + sender, no external deps. Pattern-matching addresses (`?`, `*`, `[abc]`, `{a,b}`). Bundle timetag scheduling. Echo output.
- **`SRC_OSC_F` / `SRC_OSC_TRIG` / `SRC_AUDIO` / `SRC_LINK`** — new binding source types in `input.h`.
- **`[osc]` / `[audio]` / `[link]` / `[macros]`** sections in `bindings.ini`.
- **`Input::pollOsc` / `pollAudio` / `pollLink` / `tryReload`** — per-frame drains for the new input paths plus hot-reload.
- **Synthetic ActionIds for macros** + 8-slot state snapshots.
- **Ableton Link integration** (`link_glue.h/cpp`) — header-only Link, optional via `__has_include`.
- **Syphon output** (`syphon_glue.h/mm`) — macOS only, opt-in via `SYPHON=1`.
- **Mathlab dashboard** (`overlay.cpp`) — translucent analytical view with sparklines.
- **Hot reload** — mtime polling + SIGHUP/SIGUSR1.
- **Cross-platform builds** — macOS, Linux, Windows (MSYS2 + MSVC); Link gates on `__has_include`, Syphon opts in via `SYPHON=1`.
- **CI** — GitHub Actions for all three platforms; auto-detects vendored Link submodules.
- **Homebrew formula** + **Vitepress docs site**.
- **`--list-actions`** dumps every bindable action (188 entries including macros + snapshot + math + link).

## CLI flags (new)

```
--osc-listen [PORT]       open UDP OSC listener (default 7700)
--osc-learn               print incoming OSC for mapping
--osc-echo HOST:PORT      echo every dispatched action as /cma/echo/<action>
--link                    enable Ableton Link discovery on start
--syphon [NAME]           publish render as Syphon source (macOS, opt-in build)
--list-actions            dump all action names + groups + descriptions
```

Live keys:
```
M                         toggle Mathlab dashboard
H                         help panel
```

## bindings.ini sections (new)

```ini
[osc]
listen = 7700
learn  = on
echo   = 127.0.0.1:7701
<action> = osc:/path     [scale=X] [invert] [bipolar] [delta]
<action> = osct:/path    (force trigger semantics)

[audio]
<action> = audio:rms|peak|low|mid|high   [scale=X] [invert] [bipolar]

[link]
<action> = link:phase|beat|bpm|peers     [scale=X] [invert] [bipolar]

[macros]
macro.name = action1(v1) ; action2(v2) ; action3(v3)
; bound by referencing "macro.name" on the LHS of any binding section
```

## File map

```
docs/
├── README.md                      ← you are here
├── osc/
│   ├── ARCHITECTURE.md
│   ├── PROTOCOL.md
│   ├── BINDINGS.md
│   ├── CLI.md
│   ├── COOKBOOK.md
│   ├── ACTIONS.md
│   └── TROUBLESHOOTING.md
├── features/                      ← per-feature deep dives
│   ├── HOT_RELOAD.md
│   ├── OSC_ECHO.md
│   ├── MACROS_SNAPSHOTS.md
│   ├── AUDIO_REACTIVITY.md
│   ├── ABLETON_LINK.md
│   ├── SYPHON.md
│   └── MATHLAB.md
├── launch-control/
│   ├── V1_GUIDE.md                ← original LC (2013, 2017)
│   └── XL_GUIDE.md
└── touchdesigner/
    └── GETTING_STARTED.md

bindings.examples/
├── launch_control_v1.ini
├── launch_control_xl.ini
└── crutchfield_touchdesigner.ini

development/
├── osc_send.py
└── touchdesigner/
    ├── README.md
    ├── crutchfield_osc_map.json
    ├── BUILD_NETWORK.md
    └── launch_control_xl_bridge.md
```

## The 30-second version

```bash
brew install glfw glew pkg-config
make -f Makefile.macos

# bare bones
./feedback

# the whole kit
./feedback --osc-listen --link --osc-echo 127.0.0.1:7701
# press M to open Mathlab
# plug in a Launch Control — append bindings.examples/launch_control_v1.ini

# build with Syphon (after building vendor/syphon framework)
make -f Makefile.macos SYPHON=1
./feedback --syphon
```
