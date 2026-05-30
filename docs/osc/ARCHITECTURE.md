# OSC Architecture

How the OSC layer is wired into Crutchfield's input system.

## The 30-second mental model

OSC is a **sibling input source** alongside keyboard, gamepad, and MIDI. All four sources produce the same kind of output: a call to `Input::handler_(ActionId, float)`. Everything downstream — the actual parameter writes, layer toggles, screenshots — runs through that single callback. The OSC code is purely an event-translation layer; it never touches engine state directly.

```
┌─────────────┐     ┌──────────────────┐
│  Keyboard   │────▶│                  │
└─────────────┘     │                  │
┌─────────────┐     │    Input         │     ┌─────────────────────┐
│  Gamepad    │────▶│    dispatch      │────▶│  apply_action()     │
└─────────────┘     │    (handler_)    │     │  in main.cpp        │
┌─────────────┐     │                  │     │                     │
│   MIDI      │────▶│  (ActionId,      │     │  writes to State,   │
└─────────────┘     │   float)         │     │  uniforms, layers,  │
┌─────────────┐     │                  │     │  recorder, etc.     │
│   OSC ←NEW  │────▶│                  │     └─────────────────────┘
└─────────────┘     └──────────────────┘
```

This means an OSC message can do anything a key press, a gamepad axis, or a MIDI CC can do — because they all converge on the same dispatch.

## Threading model

The OSC listener runs on a dedicated background thread (separate from MIDI's CoreMIDI callback thread and the GLFW main thread). It blocks in `recvfrom()` and pushes decoded `FeedbackOscMsg` structs into a mutex-protected `std::deque`. The main thread drains that deque every frame in `Input::pollOsc(dt)` and dispatches.

```
   Background thread (osc.cpp)          Main thread (input.cpp / main.cpp)
   ─────────────────────────             ─────────────────────────────────
        ▲                                          ▲
        │ recvfrom() (UDP)                         │ glfwSwapBuffers()
        │                                          │ ←─ frame N
        ▼                                          │
   parse OSC datagram                              │ g_input.pollOsc(dt)
        │                                          │  → feedback_osc_poll()
        ▼                                          │  → drains queue
   ┌──────────────┐                                │  → for each msg,
   │ std::mutex   │ ←──── PUSH                     │     linear-scan bindings,
   │ std::deque   │ ────▶ POP                      │     dispatch via handler_
   │ <FeedbackOsc │       (queue cap 4096; if      │
   │  Msg>        │        full, oldest dropped)   │
   └──────────────┘                                │
                                                   ▼
                                              dispatch to apply_action
```

Why this design:

- **Block-in-recv background thread**: simplest correct UDP listener. No CPU spin, no kqueue/epoll complication. The OS wakes us when a packet arrives.
- **Lock-free-ish drain on main thread**: matches the MIDI pattern exactly (see `macOS/midi_coremidi.mm` lines 95–100). One mutex, one deque. Frame-level latency.
- **Bounded queue (4096 msgs)**: if main thread stalls (e.g. a long shader compile) the listener doesn't grow unboundedly; oldest messages are dropped to keep memory flat. 4096 covers ~68 seconds at 60 fps × 1 msg/frame.
- **No new sync primitives**: same `std::mutex` + `std::deque` recipe as MIDI. Easy to audit.

## Lifecycle

```
program start
    │
    ├── parse_cli()                     ← captures --osc-listen [PORT], --osc-learn
    │       sets g_cfg.oscPort = 7700 (default if no port arg)
    │
    ├── g_input.installDefaults()       ← keyboard + MIDI defaults
    ├── g_input.setOscPort(7700)        ← if g_cfg.oscPort > 0
    ├── g_input.setOscLearn(true)       ← if g_cfg.oscLearn
    ├── g_input.setHandler(apply_action)
    ├── g_input.loadIni("bindings.ini") ← parses [osc] section,
    │                                     overrides port/learn from file if
    │                                     CLI didn't set them, populates
    │                                     bindings_ vector with SRC_OSC_F/
    │                                     SRC_OSC_TRIG entries
    │
    │   main loop (each frame)
    │       │
    │       ├── glfwPollEvents()        ← keyboard
    │       ├── pollGamepad(...)        ← gamepad
    │       ├── pollMidi(dt)            ← MIDI
    │       ├── pollOsc(dt)             ← OSC ←─ NEW
    │       │       │
    │       │       ├── lazy-open socket if oscPort_ > 0 && !oscOpened_
    │       │       │   (feedback_osc_open spawns background thread)
    │       │       │
    │       │       ├── feedback_osc_poll(buf, 256)
    │       │       │   drains up to 256 msgs from the queue
    │       │       │
    │       │       └── for each msg:
    │       │           - print learn line if oscLearn_
    │       │           - linear-scan bindings_ for SRC_OSC_F / SRC_OSC_TRIG
    │       │             matching m.address
    │       │           - dispatch via handler_(action, scaled_value)
    │       │
    │       └── render / swap buffers
    │
    └── program exit
            │
            └── ~OscState() (static at file scope in osc.cpp)
                    sets running = false
                    closes socket (unblocks recvfrom)
                    joins listener thread (or detaches as fallback)
```

The listener thread shuts down gracefully on exit because the destructor runs `running=false → close(sock) → join`. The closed socket causes `recvfrom` to return an error, the loop checks `running`, and exits. (Earlier versions crashed here — see [TROUBLESHOOTING.md#process-exit-crash](TROUBLESHOOTING.md#process-exit-crash) for the bug history.)

## How a binding gets dispatched (full trace)

Take the binding `dyn.decay.axis = osc:/cma/decay`. Walk the path of one message.

### 1. Parser builds the Binding

`Input::loadIni()` reads `bindings.ini`. Inside the `[osc]` section it sees:

```ini
dyn.decay.axis = osc:/cma/decay
```

Splits on `=` → key=`dyn.decay.axis`, value=`osc:/cma/decay`. Looks up `dyn.decay.axis` via `action_info_by_name()` — returns `ActionInfo { id = ACT_DECAY_AXIS, kind = AK_STEP }`. Sees `keyPart` starts with `osc:`, extracts `/cma/decay`, autoselects `SRC_OSC_F` because the action's kind is AK_STEP (continuous). Constructs:

```cpp
Binding b {
    action     = ACT_DECAY_AXIS,
    source     = SRC_OSC_F,
    code       = 0,                  // unused for OSC
    modmask    = 0,                  // unused for OSC
    oscAddress = "/cma/decay",
    scale      = 1.0,
    invert     = false,
    delta      = false,
    bipolar    = false,
};
bindings_.push_back(b);
```

### 2. Wire arrives

TouchDesigner OSC Out CHOP fires a UDP datagram to `127.0.0.1:7700`:

```
00 00 00 00  /  c  m  a  /  d  e  c  a  y  \0 \0 \0   ← address (4-byte aligned)
,  f  \0 \0                                            ← type tag (",f" = one float)
3F 59 99 9A                                            ← 0.85 as big-endian IEEE 754
```

Total: 20 bytes.

### 3. Listener thread parses

`listener_thread()` in `osc.cpp` wakes from `recvfrom()` with the 20-byte buffer. Checks first 8 bytes — not `#bundle\0` — so it's a single message. Calls `parse_message(buf, 20)`:

- `read_osc_string` advances offset past `/cma/decay\0\0\0\0` (12 bytes, 4-byte aligned)
- `read_osc_string` advances past `,f\0\0` (4 bytes)
- Type tag iteration: `,f` → expect one float. `read_be32` reads the 4 bytes of the float, `memcpy`s into a `float`, gets 0.85f
- Constructs `FeedbackOscMsg { address="/cma/decay", arg_type='f', arg_f=0.85f, arg_i=0 }`
- Locks `g_osc.mu`, pushes the msg onto the queue

### 4. Main thread drains + dispatches

Next frame, `Input::pollOsc(dt)` runs:

- Calls `feedback_osc_poll(buf, 256)` which under lock copies the queued msg into `buf[0]` and returns `1`
- If `oscLearn_`: prints `[osc-learn] /cma/decay f=0.8500\n`
- Iterates `bindings_`. Finds the entry where `source == SRC_OSC_F` and `oscAddress == "/cma/decay"`. Match.
- Looks up `action_info(ACT_DECAY_AXIS)` → kind is `AK_STEP`
- Since `b.bipolar = false` and `b.delta = false`, `mg = norm = 0.85`. After scale (1.0) and invert (false): `mg = 0.85`
- Switch on action kind: `AK_STEP → fire(b.action, mg)` → `handler_(ACT_DECAY_AXIS, 0.85)`

### 5. `apply_action()` runs in main.cpp

The handler set by `g_input.setHandler(apply_action)` is `apply_action` in main.cpp. It switches on `ActionId`:

```cpp
case ACT_DECAY_AXIS:
    S.decay = clamp(magnitude, 0.0f, 1.0f);
    break;
```

(Or whatever the actual write is — point being, the float lands on the simulation state and the next frame's render picks it up.)

End to end: ~one frame of latency between UDP packet arriving and the parameter changing on screen. At 60 fps that's ~16 ms worst case.

## Why no external OSC library

We considered liblo, oscpack, and Boost.Asio. Reasons we shipped a hand-written ~330 LOC parser instead:

1. **OSC 1.0 wire format is trivially small.** 4-byte-aligned strings + big-endian 32-bit ints/floats + a one-byte type tag char per arg. Bundles add a size prefix per element. The complete spec fits in a screen. liblo would add ~3 MB of code we don't need.

2. **Crutchfield ships as a single static binary.** The Windows build statically links GLFW and GLEW with no DLLs. Adding a dynamic OSC lib breaks that distribution story. A static OSC lib means downstream license review (most are LGPL).

3. **Threading model alignment.** liblo manages its own thread pool. We already have a pattern for offboard input (MIDI), and we want OSC to mirror it exactly. Hand-written means the OSC code looks like the MIDI code and reviewers don't have to learn a new model.

4. **Surface area we actually use.** We need: 4 OSC types (f, i, T, F), literal address matching, single-arg consumption. liblo's wildcard pattern matching, lo_server_thread, multi-arg dispatch, NTP timetag scheduling — all dead weight for us.

5. **Auditability.** A static-analysis pass over `osc.cpp` takes 10 minutes. A pass over liblo + its deps is days.

The trade-off: no OSC 1.1 (bundles work but timetags are ignored), no wildcard address patterns, no UDP multicast, no OSC-over-TCP, no SLIP-framed serial OSC. None are needed for TouchDesigner or any hardware controller we expect to support.

## Data structures touched

### `enum BindSource` (input.h)

Added two values:

```cpp
SRC_OSC_F,         // continuous (axis-shaped) — float arg dispatched as 0..1
SRC_OSC_TRIG,      // discrete/trigger — value > 0.5 = press, ≤ 0.5 = release
```

### `struct Binding` (input.h)

Added one field:

```cpp
std::string oscAddress;   // empty for non-OSC bindings (no heap alloc by default)
```

`std::string` default-constructs to ~24 bytes with no heap allocation, so keyboard/gamepad/MIDI bindings pay nothing for the new field.

### `Input` class (input.h)

Added:

```cpp
void pollOsc(float dt);
void setOscPort(int port);
void setOscLearn(bool enabled);
int  oscPort() const;
bool oscLearn() const;

// private:
int  oscPort_       = 0;
bool oscLearn_      = false;
bool oscOpened_     = false;   // set after first successful open
int  oscFailedPort_ = 0;       // latch: stop retrying after bind failure
```

### `enum UsageSource` (input.h)

Added one value:

```cpp
USAGE_OSC,    // for usage-logging CSV stream (when --log-usage is set)
```

### `extern "C"` interface (osc.h)

```c
int  feedback_osc_open(int port);                              // idempotent
void feedback_osc_close(void);
int  feedback_osc_poll(struct FeedbackOscMsg* out, int max);   // returns n drained
int  feedback_osc_port(void);                                  // current bound port
int  feedback_osc_connected(void);                             // 1 if open, 0 else
```

Mirrors `feedback_midi_open / feedback_midi_poll / feedback_midi_status` in `macOS/midi_coremidi.mm`.

## What the OSC code does NOT touch

- **Engine state.** Every parameter write goes through `apply_action()` in main.cpp. OSC code knows nothing about decay, blur, gamma, or layers.
- **Render thread.** OSC ingest never calls GL. The listener thread is pure userspace network + queue.
- **The MIDI path.** MIDI dispatch is untouched. The two systems coexist without interaction.
- **`bindings.ini` for non-OSC sections.** The parser branches on `section ==` and only the `[osc]` branch is new logic.

## Performance characteristics

- **Per-message dispatch cost**: O(N) where N = number of OSC bindings (linear scan). With ~50 bindings this is <1 µs per message. Negligible.
- **Queue cap**: 4096 messages. At a worst-case flood of 10,000 msg/sec the queue overruns in 0.4s and starts dropping oldest. The render thread normally drains at 60 Hz, leaving headroom for 166 msgs/frame before falling behind.
- **Memory**: `FeedbackOscMsg` is ~144 bytes (128-byte address + args). Full queue = ~590 KB. Static allocation.
- **Network**: Single UDP socket, INADDR_ANY bind. No multicast, no broadcast. Receives from any source IP.
- **Latency**: One frame worst case (~16 ms at 60 fps). Sub-frame typical (queue is usually empty when polled).
