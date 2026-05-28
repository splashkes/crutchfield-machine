# Crutchfield Machine — Documentation

In-depth docs for the OSC control layer and the Launch Control / TouchDesigner integrations added on the `osc-control` branch.

## Start here

| If you want to… | Read |
| --- | --- |
| Drive Crutchfield from a Novation Launch Control v1 (8 knobs + 16 pads + 4 side buttons) | [launch-control/V1_GUIDE.md](launch-control/V1_GUIDE.md) |
| Drive Crutchfield from a Novation Launch Control XL | [launch-control/XL_GUIDE.md](launch-control/XL_GUIDE.md) |
| Drive Crutchfield from TouchDesigner over OSC | [touchdesigner/GETTING_STARTED.md](touchdesigner/GETTING_STARTED.md) |
| Understand how the OSC layer is wired into the rest of the app | [osc/ARCHITECTURE.md](osc/ARCHITECTURE.md) |
| See every OSC binding flag and how each one behaves | [osc/BINDINGS.md](osc/BINDINGS.md) |
| Send raw OSC, troubleshoot wire-level issues | [osc/PROTOCOL.md](osc/PROTOCOL.md) |
| Look up CLI flags + `bindings.ini` `[osc]` keys | [osc/CLI.md](osc/CLI.md) |
| Find a worked recipe (audio-reactive, beat-locked, multi-source blend) | [osc/COOKBOOK.md](osc/COOKBOOK.md) |
| Browse every action you can bind | [osc/ACTIONS.md](osc/ACTIONS.md) |
| Diagnose something that isn't working | [osc/TROUBLESHOOTING.md](osc/TROUBLESHOOTING.md) |

## What's new in `osc-control`

- **`osc.h` / `osc.cpp`** — minimal OSC 1.0 UDP listener (no external deps, BSD sockets + winsock `#ifdef`), background thread, lock-protected queue.
- **`SRC_OSC_F` / `SRC_OSC_TRIG`** binding sources in `input.h`. Bindings carry an `oscAddress` string; the dispatch path matches by literal address.
- **`[osc]` section** in `bindings.ini` with `listen=PORT`, `learn=on|off`, and `osc:/path` / `osct:/path` binding specs.
- **`Input::pollOsc(dt)`** drains the OSC queue each frame and dispatches through the same `handler_(ActionId, float)` callback the MIDI layer uses.
- **CLI flags** `--osc-listen [PORT]`, `--osc-learn`, `--list-actions`.
- **Ready-to-use bindings** for the Novation Launch Control v1 (original), the Launch Control XL, and a TouchDesigner-friendly `/cma/*` namespace.
- **TouchDesigner integration** — canonical OSC address map (`crutchfield_osc_map.json`), build-the-network recipe, MIDI-to-OSC bridge guide.
- **`--list-actions`** dumps every bindable action (181 entries, grouped) so you can author bindings without spelunking the source.

## File map

```
docs/
├── README.md                                  ← you are here
├── osc/
│   ├── ARCHITECTURE.md                        ← threading, dispatch, integration
│   ├── PROTOCOL.md                            ← OSC 1.0 wire format + our parser
│   ├── BINDINGS.md                            ← every binding spec + flag
│   ├── CLI.md                                 ← flags + bindings.ini reference
│   ├── COOKBOOK.md                            ← 10+ recipes
│   ├── ACTIONS.md                             ← full action catalogue
│   └── TROUBLESHOOTING.md
├── launch-control/
│   ├── V1_GUIDE.md                            ← original LC (2013, 2017)
│   └── XL_GUIDE.md                            ← Launch Control XL
└── touchdesigner/
    └── GETTING_STARTED.md

bindings.examples/
├── launch_control_v1.ini                      ← drop into bindings.ini
├── launch_control_xl.ini
└── crutchfield_touchdesigner.ini

development/
├── osc_send.py                                ← zero-dep CLI OSC sender
└── touchdesigner/
    ├── README.md
    ├── crutchfield_osc_map.json               ← canonical address↔action map
    ├── BUILD_NETWORK.md                       ← TD network recipe
    └── launch_control_xl_bridge.md            ← LC → TD → OSC bridge

osc.h, osc.cpp                                 ← the listener
```

## The 30-second version

```bash
# 1. Build (once)
brew install glfw glew pkg-config
make -f Makefile.macos

# 2. Append a bindings example
cat bindings.examples/launch_control_v1.ini \
  >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"

# 3. Launch with OSC ingestion (port 7700)
./feedback --osc-listen --osc-learn

# 4. Plug in your Launch Control. Twiddle it. The feedback engine reacts.
#    Or send raw OSC for testing:
python3 development/osc_send.py /cma/decay 0.85
```
