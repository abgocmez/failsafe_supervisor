// Single source of monotonic time. Everything -- worker heartbeats and
// supervisor deadline checks -- uses CLOCK_MONOTONIC so an NTP step or an
// operator changing the wall clock cannot affect detection. Never CLOCK_REALTIME.
#pragma once
#include <cstdint>
#include <ctime>

namespace failsafe {

inline std::uint64_t now_mono_ns() noexcept {
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

}  // namespace failsafe
