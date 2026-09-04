// Prints CLOCK_MONOTONIC nanoseconds. Used by the measurement harness to stamp
// the fault-injection time t0 in the same clock domain as the supervisor events.
#include <cstdio>
#include "failsafe/clock.hpp"
int main() {
  std::printf("%llu\n", static_cast<unsigned long long>(failsafe::now_mono_ns()));
  return 0;
}
