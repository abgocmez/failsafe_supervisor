#!/usr/bin/env bash
# Fault-injection demo/measurement harness for the failsafe supervisor.
#   usage: inject.sh <mode> [target] [count]
#   modes: kill9   - SIGKILL the target once (crash-stop, SIGCHLD fast path)
#          stop    - SIGSTOP the target once (fail-silent, deadline slow path)
#          crashloop - SIGKILL the target repeatedly to exhaust the restart budget
# target defaults to plant. Prints the supervisor log at the end.
set -u
MODE="${1:-kill9}"
TARGET="fsw-${2:-plant}"
COUNT="${3:-6}"
ROOT="$HOME/failsafe-supervisor"
SUP="$ROOT/build/src/supervisor"
LOG="/tmp/sup.log"

cleanup() {
  [ -n "${SPID:-}" ] && kill -TERM "$SPID" 2>/dev/null
  sleep 1
  pkill -9 -x supervisor 2>/dev/null
  pkill -9 -f fsw- 2>/dev/null
  rm -f /dev/shm/failsafe_*
}
trap cleanup EXIT

# Pre-clean any stray state from a previous run.
pkill -9 -x supervisor 2>/dev/null
pkill -9 -f fsw- 2>/dev/null
rm -f /dev/shm/failsafe_* "$LOG"
sleep 1

"$SUP" >"$LOG" 2>&1 &
SPID=$!
sleep 2
echo ">>> workers up:"; pgrep -x -a fsw-plant; pgrep -x -a fsw-controller; pgrep -x -a fsw-logger

case "$MODE" in
  kill9)
    echo ">>> t=$(date +%s.%3N) SIGKILL $TARGET (SIGCHLD fast path, expect restart)"
    pkill -9 -x "$TARGET"
    sleep 3
    ;;
  stop)
    P=$(pgrep -x "$TARGET" | head -1)
    echo ">>> t=$(date +%s.%3N) SIGSTOP $TARGET pid=$P (deadline slow path)"
    kill -STOP "$P"
    sleep 3
    kill -CONT "$P" 2>/dev/null
    ;;
  crashloop)
    echo ">>> crashloop: SIGKILL $TARGET x$COUNT to exhaust the restart budget"
    for i in $(seq 1 "$COUNT"); do
      pkill -9 -x "$TARGET"
      echo "    kill $i at t=$(date +%s.%3N)"
      sleep 0.7
    done
    sleep 2
    ;;
  *)
    echo "unknown mode: $MODE"; exit 2;;
esac

echo "=================== SUPERVISOR LOG ==================="
cat "$LOG"
