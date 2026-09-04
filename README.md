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
- [ ] M3 — state machines + real safe state (GPIO)
- [ ] M4 — hardware watchdog
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
