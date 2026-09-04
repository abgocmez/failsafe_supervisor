#!/usr/bin/env bash
# M1 integration smoke test: start the supervisor (LogOutput backend, no GPIO),
# let it arm on the first heartbeat, SIGKILL the worker, and assert the
# supervisor detects the missed deadline (healthy true -> false). Proves the
# whole M1 pipeline -- fork/exec, shm heartbeat, timerfd deadline -- end to end.
set -u
SUP="$1"
LOG="$(mktemp)"
trap "rm -f \"$LOG\"; pkill -x worker 2>/dev/null; kill %1 2>/dev/null" EXIT

setsid "$SUP" >"$LOG" 2>&1 &

# Wait up to 3s for the worker to arm.
for _ in $(seq 1 60); do grep -q "armed" "$LOG" && break; sleep 0.05; done
if ! grep -q "armed" "$LOG"; then echo "FAIL: worker never armed"; cat "$LOG"; exit 1; fi

# Kill the worker and wait up to 3s for the deadline-miss detection.
pkill -9 -x worker
for _ in $(seq 1 60); do grep -q "healthy true -> false" "$LOG" && break; sleep 0.05; done

if grep -q "healthy true -> false" "$LOG"; then
  echo "m1_smoke OK"
  grep "healthy true -> false" "$LOG"
  exit 0
fi
echo "FAIL: missed-deadline not detected"; cat "$LOG"; exit 1
