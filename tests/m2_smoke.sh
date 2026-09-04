#!/usr/bin/env bash
# M2 integration smoke test (runs in CI with the LogOutput no-op backend):
#   1. SIGKILL a worker once -> assert it recovers (a second "-> Alive").
#   2. SIGKILL it in a tight loop -> assert the restart budget is spent (GaveUp).
# Proves the SIGCHLD fast path, the restart policy, and escalation end to end.
set -u
SUP="$1"
LOG="$(mktemp)"
cleanup() { kill -TERM "$SPID" 2>/dev/null; sleep 0.5;
            pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null;
            rm -f /dev/shm/failsafe_* "$LOG"; }
trap cleanup EXIT

pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null; rm -f /dev/shm/failsafe_*
"$SUP" >"$LOG" 2>&1 &
SPID=$!

# Wait for the initial three workers to arm.
for _ in $(seq 1 60); do [ "$(grep -c -- '-> Alive' "$LOG")" -ge 3 ] && break; sleep 0.05; done
if [ "$(grep -c -- '-> Alive' "$LOG")" -lt 3 ]; then echo "FAIL: workers never armed"; cat "$LOG"; exit 1; fi

# 1) Recovery: kill plant once, expect a fresh restart+Alive.
pkill -9 -x fsw-plant
for _ in $(seq 1 100); do grep -q "plant -> Restarting (exec)" "$LOG" && break; sleep 0.05; done
for _ in $(seq 1 100); do [ "$(grep -c 'plant -> Alive' "$LOG")" -ge 2 ] && break; sleep 0.05; done
if [ "$(grep -c 'plant -> Alive' "$LOG")" -lt 2 ]; then echo "FAIL: no restart recovery"; cat "$LOG"; exit 1; fi
echo "recovery OK"

# 2) Escalation: crash-loop plant to spend the budget (3 in the window).
for i in $(seq 1 6); do pkill -9 -x fsw-plant; sleep 0.7; done
for _ in $(seq 1 60); do grep -q "plant GaveUp" "$LOG" && break; sleep 0.05; done
if grep -q "plant GaveUp" "$LOG"; then
  echo "m2_smoke OK"
  grep -E "GaveUp|ESCALATE" "$LOG"
  exit 0
fi
echo "FAIL: budget never exhausted"; cat "$LOG"; exit 1
