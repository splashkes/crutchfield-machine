# OSC Troubleshooting

Symptoms → causes → fixes. Walk the table top-down; the diagnostics build on each other.

## Quick diagnostic chain

```
1. Is the OSC socket bound?         → lsof -nP -iUDP:7700
2. Is the message arriving?         → tcpdump -i lo0 -X 'udp port 7700'
3. Is the parser accepting it?      → ./feedback --osc-listen --osc-learn
4. Is a binding matching?           → grep your action.name in bindings.ini
5. Is the binding pointing to a real action? → ./feedback --list-actions | grep <name>
6. Is the action's kind compatible? → see [BINDINGS.md](BINDINGS.md) dispatch table
7. Is apply_action() handling it?   → trace into main.cpp's apply_action()
```

If step 1 fails, the rest can't happen. If 1-4 pass but 5 fails, you have a typo in the action name. If 1-5 pass but the parameter doesn't move, you're on step 6 or 7.

## Symptoms

### `[osc] bind to port 7700 failed`

**Cause**: another process owns the port.

**Diagnose**:

```bash
lsof -nP -iUDP:7700
```

If it shows another `feedback` process, you have a zombie (probably from a previous crash):

```bash
pkill -9 -f "crutchfield-machine/feedback"
sleep 1
lsof -nP -iUDP:7700    # should be empty
```

If it shows a different process (TouchDesigner, Max, etc.), either close that process or change Crutchfield's port:

```bash
./feedback --osc-listen 9000
```

…and update your sender accordingly.

### Bind succeeds but no `[osc-learn]` lines appear when I send

**Diagnose tier 1**: confirm packets actually leave the sender.

```bash
sudo tcpdump -i lo0 'udp port 7700'    # localhost
# or
sudo tcpdump -i en0 'udp port 7700'    # over the network
```

If `tcpdump` shows nothing, the sender isn't actually sending. Check the sender's host/port config.

**Diagnose tier 2**: packets arrive but parser rejects.

```bash
sudo tcpdump -i lo0 -X 'udp port 7700'
```

Look for malformed packets:
- Address doesn't start with `/` (offset 0 of payload) → rejected
- Type tag doesn't start with `,` → treated as no-arg message, dispatched as `(no args)` learn line
- Unknown type tag char (e.g. `h` for int64) → entire message bailed, no learn line
- Multi-bundle nested deeply → all parsed but each contained message handled separately

**Diagnose tier 3**: log output buffering.

If you redirected stdout to a file with `> file.log` or `nohup`, glibc block-buffers stdout. Learn lines won't appear until the buffer fills. Fix:

```bash
stdbuf -oL ./feedback --osc-listen --osc-learn > file.log
# or
./feedback --osc-listen --osc-learn 2>&1 | tee file.log
```

### `[bindings] unknown action 'foo' — skipped`

**Cause**: typo or wrong action name.

**Fix**: dump the catalogue and grep:

```bash
./feedback --list-actions | grep -i <partial-name>
```

Common gotchas:
- `decay` vs `dyn.decay.axis` — actions are namespaced
- `screenshot` vs `app.screenshot` — same
- `+` and `-` are literal name suffixes for STEP actions (`brightness+`, `chroma+`)

Action names are case-sensitive.

### Learn line shows message, binding exists, parameter doesn't move

**Diagnose tier 1**: confirm the binding is loaded.

After Crutchfield exits cleanly, the effective bindings are written back to `bindings.ini`. Check:

```bash
grep "<action.name>" "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"
```

If the line isn't there after a clean exit, the parser likely rejected it (see "unknown action" above).

**Diagnose tier 2**: confirm the action kind is what you expect.

The dispatch path differs by kind. Specifically:

- `AK_DISCRETE` actions fire **only** when `value > 0.5`. Sending `/cma/layer/noise 0` or `/cma/layer/noise 0.3` does nothing.
- `AK_TRIGGER` actions fire on both edges. Sending `0` releases.
- `AK_STEP` actions integrate the value as a nudge — sending `0` does nothing visible (zero nudge).
- `AK_RATE` actions accumulate per frame — useful for "speed of change" semantics, not absolute position.

For "the parameter just snaps to whatever I send", you want an `AK_STEP`/`AK_RATE` action with a `setAxis` or `.axis` suffix (e.g. `dyn.decay.axis`, not `dyn.decay+`).

**Diagnose tier 3**: confirm the action's handler is non-trivial.

Some actions are placeholders or no-ops in certain builds. Search `apply_action` in `main.cpp` for the `case ACT_FOO:` block — if it's missing, the action exists in the catalogue but isn't wired to anything yet.

### Continuous parameter "jumps" instead of sweeping

**Cause**: your sender is sending discrete snapshots, not continuous values.

If you're driving from TD: make sure `OSC Out CHOP > Send Mode` is `Send Channel Values` (not `Send Channel Names + Values`) and `Send Every Frame` is **on**. Without "every frame", TD only sends on change events which can feel chunky.

If you're driving from a Python script: send values at >= 30 Hz (every 33ms). Lower rates produce visible stepping.

### Parameter overshoots / oscillates

**Cause**: the source is sending faster than the render can keep up, queue overflows, oldest values dropped, the binding sees a stale sample after the latest.

This is rare in practice (queue holds 4096 msgs ≈ 68s of 60 Hz). If you see it, your source is sending >10k msg/sec which is overkill.

**Fix**: throttle the source. TD: insert a `Resample CHOP` with target rate 60. Python: add `time.sleep(0.016)` between sends.

### Random spikes / wrong values

**Cause**: another OSC source is also sending to that address.

The listener doesn't track senders — all packets to the port are dispatched. If two processes both send `/cma/decay`, the parameter wobbles between their values.

**Diagnose**: `tcpdump -X` and look at source IPs.

**Fix**: stop the rogue sender, or rename the address in one of them.

### Process exit crash

**History**: pre-v1 the static `OscState` destructor unwound at program exit with a still-joinable thread, triggering `std::terminate` → SIGABRT. Crash report cited `OscState::~OscState()`.

**Fixed** in commit `2674f31` by adding an explicit destructor that:
1. Sets `running = false` (atomic)
2. Closes the socket (unblocks any `recvfrom`)
3. Joins the listener thread (with `detach` as fallback)

If you see a SIGABRT on exit citing OscState, you're on an old build. Pull, rebuild.

### Cross-platform note: Windows / Linux builds

The macOS build is what's validated end-to-end. Linux / Windows wiring exists in `linux/Makefile` and `build_msvc.bat` (osc.cpp + ws2_32 for Windows winsock) but hasn't been exercised on real hardware. If you build on a non-Mac platform and OSC misbehaves, file an issue with:

- OS + version
- Compiler + version
- `lsof -i :7700` (or `netstat -anu | grep 7700` on Windows)
- The startup log up to the first OSC mention
- A `tcpdump` capture of one inbound message

### "Permission denied" on UDP bind

Linux: ports < 1024 require root. Use 7700 or any port ≥ 1024.

macOS: SIP-protected sandbox or `Privacy & Security > Network` may prompt. Allow Crutchfield network access on first run.

### TouchDesigner sends but Crutchfield doesn't react

**Quick check**: in TD's OSC Out CHOP, set `Active` to off then on. Some TD versions need a kick after editing.

**Next check**: TD's channel names need slashes that match your binding addresses. The CHOP turns channel name `cma/decay` into OSC address `/cma/decay` (TD prepends the slash). If your channel is `cmadecay`, the address sent is `/cmadecay` and won't match `/cma/decay`.

### Launch Control connects via MIDI but knobs don't move parameters

This is a MIDI path issue, not OSC. Diagnose with:

```bash
./feedback --midi-learn
```

Twiddle a knob. The console should print:

```
[midi-learn] ch=9 cc:21 val=42
```

If you see the learn line: the controller is connected, MIDI is flowing, your binding doesn't match the channel/CC. Update your binding.

If you see nothing: the controller isn't being seen. Check `Audio MIDI Setup.app` on macOS. The CoreMIDI source name must contain the substring in `[midi] port = ...` in your `bindings.ini`. For LC v1 the default `port = Launch Control` matches the actual device name "Launch Control".

If port matches but still nothing: the controller might be on a different MIDI port from what macOS shows. Try unplugging/replugging USB.

## Reset to known-good state

When all else fails:

```bash
# 1. Stop all feedback processes
pkill -9 -f "crutchfield-machine/feedback"

# 2. Remove user bindings (CAUTION: loses any edits)
mv "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini" \
   "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini.bak"

# 3. Rebuild
cd ~/workspace/crutchfield-machine
make -f Makefile.macos clean
make -f Makefile.macos

# 4. Launch with verbose OSC
./feedback --osc-listen --osc-learn

# 5. A fresh bindings.ini is created. Re-append your example:
cat bindings.examples/launch_control_v1.ini \
  >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini"

# 6. Restart and test
```
