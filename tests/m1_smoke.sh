#!/usr/bin/env bash
# Deadline-path integration test (runs in CI with the LogOutput backend):
# start the supervisor, let its workers arm, SIGSTOP one (fail-silent -- alive
# but no heartbeat), and assert the supervisor detects it via the deadline path.
# This is the slow-path counterpart to m2_smoke's SIGCHLD fast path.
set -u
SUP="$1"
LOG="$(mktemp)"
STOPPED=""
cleanup() { [ -n "$STOPPED" ] && kill -CONT "$STOPPED" 2>/dev/null;
            kill -TERM "$SPID" 2>/dev/null; sleep 0.5;
            pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null;
            rm -f /dev/shm/failsafe_* "$LOG"; }
trap cleanup EXIT

pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null; rm -f /dev/shm/failsafe_*
"$SUP" >"$LOG" 2>&1 &
SPID=$!

for _ in $(seq 1 60); do [ "$(grep -c -- '-> Alive' "$LOG")" -ge 3 ] && break; sleep 0.05; done
if [ "$(grep -c -- '-> Alive' "$LOG")" -lt 3 ]; then echo "FAIL: workers never armed"; cat "$LOG"; exit 1; fi

# Freeze one worker: alive but silent. Only the deadline path can catch this.
STOPPED="$(pgrep -x fsw-logger | head -1)"
kill -STOP "$STOPPED"
for _ in $(seq 1 100); do grep -q "logger failed via deadline (slow)" "$LOG" && break; sleep 0.05; done

if grep -q "logger failed via deadline (slow)" "$LOG"; then
  echo "deadline_detect OK"
  grep "via deadline (slow)" "$LOG"
  exit 0
fi
echo "FAIL: deadline miss not detected"; cat "$LOG"; exit 1
