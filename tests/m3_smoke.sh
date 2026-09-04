#!/usr/bin/env bash
# M3 integration test (runs in CI with the LogSafetyIo backend): drive a worker
# to GaveUp, assert the system latches into SafeState (ENABLE off), then ack via
# SIGUSR1 and assert it re-arms back to Running (ENABLE on). Proves the system
# state machine and the latched safe state end to end, without hardware.
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

# Reach Running.
for _ in $(seq 1 80); do grep -q "\-> Running" "$LOG" && break; sleep 0.05; done
if ! grep -q "\-> Running" "$LOG"; then echo "FAIL: never reached Running"; cat "$LOG"; exit 1; fi

# Crash-loop plant to exhaust its budget and force SafeState.
for i in $(seq 1 6); do pkill -9 -x fsw-plant; sleep 0.7; done
for _ in $(seq 1 60); do grep -q "\-> SafeState" "$LOG" && break; sleep 0.05; done
if ! grep -q "\-> SafeState" "$LOG"; then echo "FAIL: did not enter SafeState"; cat "$LOG"; exit 1; fi
if ! grep -q "ENABLE=off" "$LOG"; then echo "FAIL: ENABLE not dropped in SafeState"; cat "$LOG"; exit 1; fi
echo "safe-state latched OK"

# Ack via SIGUSR1 and expect re-arm back to Running.
kill -USR1 "$SPID"
for _ in $(seq 1 100); do grep -q "safe state cleared" "$LOG" && break; sleep 0.05; done
for _ in $(seq 1 100); do [ "$(grep -c 'ENABLE=on' "$LOG")" -ge 2 ] && break; sleep 0.05; done

if grep -q "safe state cleared" "$LOG" && [ "$(grep -c 'ENABLE=on' "$LOG")" -ge 2 ]; then
  echo "m3_smoke OK"
  grep -E "SYSTEM|SAFE|ack" "$LOG"
  exit 0
fi
echo "FAIL: did not re-arm after ack"; cat "$LOG"; exit 1
