#!/usr/bin/env bash
# Measure supervisor tick jitter, before/after RT tuning, with and without load.
#   usage: jitter.sh <baseline|rt> <seconds> [with-load]
set -u
MODE="${1:-baseline}"
SECS="${2:-30}"
LOAD="${3:-noload}"
ROOT="$HOME/failsafe-supervisor"
SUP="$ROOT/build/src/supervisor"   # HAVE_GPIO build (LogSafetyIo used off-Pi is fine too)
LOG="/tmp/jitter_${MODE}_${LOAD}.log"

pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null; rm -f /dev/shm/failsafe_* "$LOG"
sleep 1

# Optional load: saturate all 4 cores with busy loops.
LOAD_PIDS=()
if [ "$LOAD" = "with-load" ]; then
  for _ in 1 2 3 4; do sh -c 'while :; do :; done' & LOAD_PIDS+=($!); done
  echo ">>> load: 4 busy loops running"
fi

if [ "$MODE" = "rt" ]; then
  # RT needs root; pin to CPU 3.
  sudo FAILSAFE_RT=1 FAILSAFE_CPU=3 "$SUP" >"$LOG" 2>&1 &
else
  "$SUP" >"$LOG" 2>&1 &
fi
SPID=$!
sleep "$SECS"
sudo kill -TERM "$SPID" 2>/dev/null; kill -TERM "$SPID" 2>/dev/null
sleep 1

for p in "${LOAD_PIDS[@]:-}"; do [ -n "$p" ] && kill -9 "$p" 2>/dev/null; done
sudo pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null; sudo rm -f /dev/shm/failsafe_*

echo "MODE=$MODE LOAD=$LOAD:"
grep -E "rt|JITTER" "$LOG"
