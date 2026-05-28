#!/usr/bin/env bash
# launch.sh — Sean's default Crutchfield Machine launch.
#
# Bakes in the full kit: OSC ingest + Ableton Link + OSC echo for
# TouchDesigner UI mirror + Syphon publishing for the VJ chain.
#
# Pre-flight kills any leftover instance holding UDP 7700 so a fresh
# launch always gets a clean port. Stdout + stderr go to a log file
# under /tmp so the running process doesn't block on terminal output.
#
# Usage:
#   ./launch.sh                  # windowed, MUSIC OFF (silent)
#   ./launch.sh --music          # windowed + bundled metronome on
#   ./launch.sh --fullscreen     # fullscreen at native resolution, silent
#   ./launch.sh --hi             # max colour pipeline + fullscreen, silent
#   ./launch.sh tail             # tail the running session log
#   ./launch.sh stop             # clean shutdown
#   ./launch.sh kill             # force kill
#   ./launch.sh status           # is it running? + what's on port 7700?
#
# Anything else gets passed straight through to ./feedback, so you can
# do e.g. ./launch.sh --sim-res 3840x2160 to override sim resolution.

set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
BIN="$REPO/feedback"
LOG="/tmp/crutchfield-session.log"
PID_FILE="/tmp/crutchfield.pid"
OSC_PORT=7700
ECHO_PORT=7701
SYPHON_NAME="Crutchfield"

# ── Commands ─────────────────────────────────────────────────────────
case "${1:-}" in
  stop)
    if [[ -f "$PID_FILE" ]]; then
      PID=$(cat "$PID_FILE")
      kill -TERM "$PID" 2>/dev/null || true
      echo "sent SIGTERM to pid $PID"
      sleep 1
      rm -f "$PID_FILE"
    else
      pkill -TERM -f "crutchfield-machine/feedback" 2>/dev/null && \
        echo "killed running feedback" || \
        echo "(no feedback process found)"
    fi
    exit 0
    ;;
  kill)
    pkill -9 -f "crutchfield-machine/feedback" 2>/dev/null && \
      echo "force-killed feedback" || echo "(no process found)"
    PIDS=$(lsof -nP -iUDP:"$OSC_PORT" -t 2>/dev/null || true)
    for p in $PIDS; do kill -9 "$p" 2>/dev/null || true; done
    rm -f "$PID_FILE"
    exit 0
    ;;
  status)
    echo "=== process ==="
    pgrep -fl "crutchfield-machine/feedback" || echo "(not running)"
    echo ""
    echo "=== port $OSC_PORT ==="
    lsof -nP -iUDP:"$OSC_PORT" 2>/dev/null || echo "(free)"
    echo ""
    echo "=== last 8 log lines ==="
    [[ -f "$LOG" ]] && tail -8 "$LOG" || echo "(no log yet)"
    exit 0
    ;;
  tail)
    exec tail -f "$LOG"
    ;;
  --hi)
    shift
    set -- --fullscreen --high-color "$@"
    ;;
esac

# ── Pre-flight: clear UDP 7700 if held by a zombie ──────────────────
PIDS=$(lsof -nP -iUDP:"$OSC_PORT" -t 2>/dev/null || true)
if [[ -n "$PIDS" ]]; then
  echo "[launch] killing leftover process(es) on UDP $OSC_PORT: $PIDS"
  for p in $PIDS; do kill -9 "$p" 2>/dev/null || true; done
  sleep 1
fi

# ── The kit ──────────────────────────────────────────────────────────
ARGS=(
  --osc-listen "$OSC_PORT"
  --link
  --osc-echo "127.0.0.1:$ECHO_PORT"
)

# Syphon only if the build was made with SYPHON=1. The flag is silently
# ignored when SYPHON=0 so it's safe to pass either way.
ARGS+=(--syphon "$SYPHON_NAME")

# The binary now defaults to silent (a stdout + on-screen HUD hint
# tells the user how to start the metronome). --music on the command
# line turns it on at boot. We just pass user args through; no
# translation needed since both --music and --no-music are valid
# feedback flags now.

# Pass through any additional flags the user gave.
ARGS+=("$@")

# ── Launch (stdbuf -oL keeps the log line-buffered for live tailing) ─
echo "[launch] $BIN ${ARGS[*]}"
echo "[launch] log: $LOG"
echo "[launch] pid: $PID_FILE"

# Truncate previous session log so 'tail -f' shows just this run.
: > "$LOG"

# nohup detaches from terminal so the session survives closing the
# shell. The PID we record is the actual feedback PID, not nohup's.
nohup stdbuf -oL "$BIN" "${ARGS[@]}" > "$LOG" 2>&1 &
PID=$!
echo "$PID" > "$PID_FILE"

# Give it a moment to start so we can confirm.
sleep 2

if kill -0 "$PID" 2>/dev/null; then
  echo "[launch] running (pid $PID)"
  echo ""
  echo "=== first log lines ==="
  head -20 "$LOG" 2>/dev/null
  echo ""
  echo "tail with:   ./launch.sh tail"
  echo "stop with:   ./launch.sh stop"
  echo "status with: ./launch.sh status"
else
  echo "[launch] FAILED to start — see $LOG" >&2
  tail -30 "$LOG" >&2
  exit 1
fi
