#!/usr/bin/env bash
# Measurement harness: inject one fault class N times and record detection and
# recovery latency in a shared clock domain. t0 = CLOCK_MONOTONIC (now_ns) at
# injection; t1 = the supervisor's structured "EV <mono_ns> ..." event. Both are
# the same clock (all processes on this Pi), so latency = t1 - t0 is exact.
#   usage: measure.sh <kill9|stop|stall> <trials> <csv_out>
set -u
MODE="${1:-kill9}"
N="${2:-100}"
CSV="${3:-results/latency.csv}"
ROOT="$HOME/failsafe-supervisor"
SUP="$ROOT/build-ci/src/supervisor"     # LogSafetyIo backend: no GPIO, no root
NOW="$ROOT/build-ci/src/now_ns"
LOG="/tmp/measure_${MODE}.log"
NAMES=(plant controller logger)

cleanup() { kill -TERM "$SPID" 2>/dev/null; sleep 0.3;
            pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null;
            rm -f /dev/shm/failsafe_*; }
trap cleanup EXIT

pkill -9 -x supervisor 2>/dev/null; pkill -9 -f fsw- 2>/dev/null; rm -f /dev/shm/failsafe_* "$LOG"
sleep 1

# Huge restart budget so repeated injection never trips escalation.
FAILSAFE_MAX_RESTARTS=1000000 "$SUP" >"$LOG" 2>&1 &
SPID=$!
for _ in $(seq 1 80); do [ "$(grep -c 'EV .* ALIVE' "$LOG")" -ge 3 ] && break; sleep 0.05; done

mkdir -p "$(dirname "$CSV")"
[ -f "$CSV" ] || echo "class,trial,target,detect_ms,recover_ms,path" > "$CSV"

wait_event() {  # <from_line> <kind_regex> <worker> -> prints "ns kind"
  local from="$1" kind="$2" w="$3" line=""
  for _ in $(seq 1 400); do
    line=$(tail -n +"$from" "$LOG" | grep -m1 -E "^EV [0-9]+ ${kind} ${w}\$")
    [ -n "$line" ] && { echo "$line" | awk '{print $2, $3}'; return 0; }
    sleep 0.02
  done
  return 1
}

ok=0; fail=0
for i in $(seq 1 "$N"); do
  T="${NAMES[$((i % 3))]}"
  # Ensure the target is alive before injecting.
  for _ in $(seq 1 100); do pgrep -x "fsw-$T" >/dev/null && break; sleep 0.02; done
  OLD=$(pgrep -x "fsw-$T" | head -1)
  L0=$(( $(wc -l < "$LOG") + 1 ))

  # kill is a bash builtin (near-zero overhead); using the captured pid avoids
  # pkill's ~40ms /proc scan, which would otherwise dominate the measurement.
  t0=$("$NOW")
  case "$MODE" in
    kill9) kill -9 "$OLD" 2>/dev/null ;;
    stop)  kill -STOP "$OLD" 2>/dev/null ;;
    stall) kill -USR1 "$OLD" 2>/dev/null ;;
    *) echo "unknown mode $MODE"; exit 2 ;;
  esac

  D=$(wait_event "$L0" "DETECT_(SIGCHLD|DEADLINE)" "$T") || { fail=$((fail+1)); continue; }
  t1=$(echo "$D" | awk '{print $1}'); path=$(echo "$D" | awk '{print $2}')
  A=$(wait_event "$L0" "ALIVE" "$T") || { fail=$((fail+1)); continue; }
  t2=$(echo "$A" | awk '{print $1}')

  det=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.3f",(b-a)/1e6}')
  rec=$(awk -v a="$t0" -v b="$t2" 'BEGIN{printf "%.3f",(b-a)/1e6}')
  echo "$MODE,$i,$T,$det,$rec,$path" >> "$CSV"
  ok=$((ok+1))

  # Clean up the injected instance for stop/stall (crash-stop already died).
  [ "$MODE" != "kill9" ] && kill -9 "$OLD" 2>/dev/null
  sleep 0.4
done

echo "measure $MODE: ok=$ok fail=$fail -> $CSV"
