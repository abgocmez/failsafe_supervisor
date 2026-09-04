#!/usr/bin/env bash
# M6 asymmetry test (runs in CI, no Docker): an owned worker is crash-detected
# and restarted; an attached external worker can only be deadline-detected and
# escalates (no restart). This is the logic behind the two-container demo.
set -u
SUP="$1"
WK="$2"
SHM="/failsafe_m6_$$"
LOG="$(mktemp)"
cleanup() { kill -TERM "$SPID" 2>/dev/null; kill -9 "$WPID" 2>/dev/null; sleep 0.3;
            pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null;
            rm -f "/dev/shm${SHM}" "$LOG"; }
trap cleanup EXIT

pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null
FAILSAFE_SHM_NAME="$SHM" FAILSAFE_EXTERNAL=1 FAILSAFE_MAX_RESTARTS=1000000 \
  "$SUP" >"$LOG" 2>&1 &
SPID=$!
for _ in $(seq 1 80); do [ "$(grep -c 'EV .* ALIVE' "$LOG")" -ge 3 ] && break; sleep 0.05; done

# Attach an external worker to slot 3.
"$WK" "$SHM" 3 100 fsw-external &
WPID=$!
for _ in $(seq 1 100); do grep -q "external0 -> Alive" "$LOG" && break; sleep 0.05; done
grep -q "external0 -> Alive" "$LOG" || { echo "FAIL: external never attached"; cat "$LOG"; exit 1; }
echo "external attached OK"

# Owned worker: crash -> restart.
kill -9 "$(pgrep -x fsw-plant | head -1)"
for _ in $(seq 1 100); do grep -q "plant -> Restarting" "$LOG" && break; sleep 0.05; done
grep -q "plant -> Restarting" "$LOG" || { echo "FAIL: owned worker did not restart"; cat "$LOG"; exit 1; }
echo "owned restart OK"

# External worker: kill -> escalate, no restart.
kill -9 "$WPID"
for _ in $(seq 1 200); do grep -q "external0 cannot be restarted" "$LOG" && break; sleep 0.05; done
if grep -q "external0 cannot be restarted" "$LOG" \
   && grep -q "Running -> SafeState" "$LOG" \
   && ! grep -q "external0 -> Restarting" "$LOG"; then
  echo "m6_smoke OK"
  grep -E "external0|SafeState|ESCALATE" "$LOG"
  exit 0
fi
echo "FAIL: external asymmetry not observed"; cat "$LOG"; exit 1
