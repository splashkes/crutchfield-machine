# CLI & config reference

Every command-line flag and every `bindings.ini` `[osc]` key that the OSC layer recognises, plus the precedence rules between them.

## CLI flags

| Flag | Arg | Effect |
| --- | --- | --- |
| `--osc-listen [PORT]` | optional int (default `7700`) | Open the UDP listener on the given port. If the arg is missing or non-numeric, defaults to 7700. |
| `--osc-learn` | none | Print every incoming OSC message to stdout. Implies `--osc-listen 7700` if no port set yet. |
| `--list-actions` | none | Dump every bindable action.name + group + description to stdout, then exit 0. |

These join the existing flags (`--fullscreen`, `--sim-res`, etc.) — see `./feedback --help` for the full set.

### Flag precedence

CLI > `bindings.ini` > built-in default.

If you pass `--osc-listen 9000` and the INI says `listen = 7700`, the CLI wins; the listener binds 9000. If you pass `--osc-learn`, learn mode is on even if the INI says `learn = off`.

If you pass neither and the INI is silent, the listener does not start (port 0 means disabled).

## `bindings.ini` `[osc]` section

```ini
[osc]
listen = 7700                  # int, UDP port. 0 = disabled.
learn  = on                    # on|off|true|false|yes|no|1|0

# Bindings (one per line)
<action.name> = osc:/path  [scale=X] [invert] [bipolar] [delta]
<action.name> = osct:/path [scale=X] [invert]
```

Section-level keys (`listen`, `learn`) and binding lines can interleave in any order. Comments use `#`. Empty lines are ignored.

See [BINDINGS.md](BINDINGS.md) for full binding syntax + flag semantics.

## Examples

### Bare-minimum first launch

```bash
./feedback --osc-listen --osc-learn
```

Binds port 7700, prints every incoming message. Sends with no matching binding still appear in learn output but don't dispatch anywhere.

### Custom port

```bash
./feedback --osc-listen 9000
```

Useful when 7700 is taken or your TD project already targets a different port. Note: change all your sender configs to match.

### Persistent config (no flags needed on launch)

```bash
# Edit bindings.ini
cat >> "$HOME/Library/Application Support/Crutchfield Machine/bindings.ini" <<'EOF'

[osc]
listen = 7700
learn  = off
EOF

# Now this works without flags
./feedback
```

### Discover the action catalogue

```bash
./feedback --list-actions | less

# Search for axis actions:
./feedback --list-actions | grep '\.axis\|\.setAxis'

# Search by group:
./feedback --list-actions | grep '\[Inject\]'
```

181 entries total. See [ACTIONS.md](ACTIONS.md) for the grouped catalogue.

### Combine with other flags

```bash
./feedback --osc-listen --osc-learn --fullscreen --high-color
```

OSC ingestion + max colour pipeline + fullscreen. The flags are independent.

## Environment variables

None at this layer. The OSC subsystem does not read environment variables. Configure exclusively through CLI or `bindings.ini`.

## Output

### Startup

If OSC is enabled, you'll see one line at the start of the first frame's `pollOsc`:

```
[osc] listening on UDP port 7700
```

If the bind fails (port collision):

```
[osc] bind to port 7700 failed
```

The bind-fail is latched — we don't retry every frame after the first failure (would spam the log). To recover: stop the conflicting process, change the port, or restart Crutchfield.

### Per-message (learn mode)

```
[osc-learn] /cma/decay f=0.8500
[osc-learn] /cma/layer/noise T
[osc-learn] /cma/preset/select i=3
[osc-learn] /cma/shot (no args)
[osc-learn] /cma/something/weird s
[osc-learn] /unknown/addr f=0.5000     ← arrived but no binding matches; dispatched nowhere
```

Format:
- `f=` prefix → float arg
- `i=` prefix → int32 arg
- `T` / `F` → boolean
- `s` → string (logged, value ignored)
- `(no args)` → empty message; treated as bang/trigger

### Shutdown

```
[osc] closed
```

Only printed if `feedback_osc_close()` is called explicitly (e.g. via `--list-actions` or some future runtime re-config). On normal exit the destructor runs and the close is silent.

## Stdout buffering caveat

When you pipe Crutchfield's stdout to a file (`./feedback > log.txt`) or run under `nohup`, glibc switches stdout to **block buffering** instead of line buffering. OSC log lines won't appear in the file until the buffer fills (~4 KB) or the process exits.

For interactive debugging, either:

```bash
# Force line-buffered stdout
stdbuf -oL ./feedback --osc-listen --osc-learn > log.txt

# Or pipe to cat (which is line-buffered by default)
./feedback --osc-listen --osc-learn | cat > log.txt

# Or tail the log live as it's written
./feedback --osc-listen --osc-learn 2>&1 | tee -a log.txt
```

Without one of these, your log will look empty for a while then suddenly catch up.

## Programmatic API (C / C++)

The OSC layer exposes a `extern "C"` interface in `osc.h`:

```c
int  feedback_osc_open(int port);
void feedback_osc_close(void);
int  feedback_osc_poll(struct FeedbackOscMsg* out, int max_count);
int  feedback_osc_port(void);
int  feedback_osc_connected(void);
```

These are used internally by `Input::pollOsc()`. External callers (e.g. an embedding host) can call them directly. `feedback_osc_open` is idempotent — calling twice with the same port is a no-op; calling with a different port closes the previous listener first.

`feedback_osc_poll` returns the number of messages drained from the queue (0..`max_count`). Non-blocking.

`FeedbackOscMsg` layout (from `osc.h`):

```c
struct FeedbackOscMsg {
    char    address[128];
    float   arg_f;
    int32_t arg_i;
    char    arg_type;   // 'f', 'i', 'T', 'F', 's', or 0
    uint8_t reserved[3];
};
```

Stable ABI. Safe to mmap, log, or pass through FFI boundaries.

## Cleanup on exit

`OscState::~OscState()` (static at file scope in `osc.cpp`) runs on every clean program exit. It:

1. Sets `running` to false (atomic)
2. Closes the socket (unblocks any waiting `recvfrom`)
3. Joins the listener thread (with detach as a fallback if join would hang)

This means SIGTERM, `exit()`, and natural return-from-main all shut down cleanly. SIGKILL bypasses the destructor (the process just dies) — the OS reclaims the socket and the thread on its own. Either way, no zombies, no resource leaks.
