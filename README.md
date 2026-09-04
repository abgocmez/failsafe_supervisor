# Failsafe Supervisor

![ci](https://github.com/abgocmez/failsafe_supervisor/actions/workflows/ci.yml/badge.svg)

A C++ daemon for a Linux edge device (Raspberry Pi 3 B+) that supervises worker
processes over shared-memory heartbeats, detects missed deadlines against a
monotonic clock, restarts them under a budgeted policy, and, when a worker
cannot be recovered, drives the system into a defined **safe state**.

Goal: inject faults systematically and measure recovery latency per fault class.

## Status
- [x] M0 — bring-up: toolchain, CMake, CI, first LED
- [x] M1 — heartbeat + deadline detection
- [x] M2 — restart policy + SIGCHLD fast path
- [x] M3 — state machines + latched safe state (logic, CI, and on-Pi LED/button verified)
- [x] M4 — hardware watchdog (BCM /dev/watchdog; SIGSTOP supervisor -> Pi reboots, verified)
- [ ] M5 — fault injection + measurement
- [ ] M6 — containers

## Target hardware
Raspberry Pi 3 B+, Raspberry Pi OS (Debian 13 trixie, aarch64), GCC 14, libgpiod 2.

## Build
On the Pi (with hardware backend):
\`\`\`sh
cmake -S . -B build -G Ninja -DHAVE_GPIO=ON
cmake --build build
ctest --test-dir build --output-on-failure
\`\`\`
CI builds and tests off-target on x86_64 with \`HAVE_GPIO=OFF\`, so the code base
stays portable and the hardware layer sits behind a compile-time switch.

## Design notes
- **Clocks.** Deadline tracking uses \`CLOCK_MONOTONIC\`, never wall-clock time, so
  an NTP step or an operator changing the date cannot affect detection. The Pi
  has no RTC, so \`CLOCK_REALTIME\` jumps by decades once NTP syncs; \`sanity\`
  prints both clocks to make that gap visible.
- **Lock-free heartbeat.** The heartbeat record is two 64-bit atomics, which are
  natively lock-free on aarch64. \`sanity\` asserts this at run time; a 32-bit
  image would silently break the "lock-free" claim.

## Out of scope (deliberate)
Two-node failover is out of scope: with two nodes you cannot form a quorum, so a
network partition yields split-brain -- two supervisors each asserting ownership
of the actuator, which is worse than the fault it was meant to prevent. Doing it
correctly requires a third node or external arbitration plus fencing; noted as
future work rather than half-implemented here.

## Hardware watchdog (who watches the watcher)

The supervisor kicks the BCM2835 hardware watchdog (`/dev/watchdog0`) every tick
while its event loop is alive. If the supervisor hangs or dies, the kicks stop
and the board reboots after ~15 s; a reset drives every GPIO back to input, so
ENABLE falls and the heater goes off -- a dead supervisor still converges on the
safe state. This is the answer to "who supervises the supervisor," and it is
much stronger than a second supervisor process (which just moves the question).

- Raspberry Pi OS gives systemd the hardware watchdog by default
  (`RuntimeWatchdogSec=1m`). `scripts/setup-pi.sh` overrides that to 0 so the
  supervisor can own the device directly; run the supervisor as root.
- Clean shutdown writes the magic character `V` before closing, disarming the
  watchdog so stopping the supervisor normally never reboots the box.
- Kicked in every state including SafeState: the watchdog guards supervisor
  *liveness*, which is a separate concern from plant safety. A latched SafeState
  is a handled state, not a hang.
- Verified on hardware: `SIGSTOP` the supervisor and the Pi reboots ~15 s later
  (boot id changes, uptime resets).
- Alternative on a systemd host: run as a service with `WatchdogSec` +
  `sd_notify(WATCHDOG=1)`, layering app -> systemd -> hardware. Chosen the direct
  route so the reboot is attributable to our own code and mirrors an MCU IWDG.
