// M0 sanity: verifies the preconditions of the M1 heartbeat protocol.
// It checks that the heartbeat record will be lock-free, and prints the two
// clock sources so the MONOTONIC-vs-REALTIME gap is visible.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace {
// Core of the M1 heartbeat record: two 64-bit atomic fields.
// On aarch64 these must be natively lock-free. If they are not, the
// "lock-free" heartbeat claim is invalid -- catch it at run time, not
// at compile time.
struct HeartbeatCore {
  std::atomic<std::uint64_t> seq;
  std::atomic<std::uint64_t> mono_ns;
};

std::uint64_t now_ns(clockid_t clk) {
  timespec ts{};
  clock_gettime(clk, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}
}  // namespace

int main() {
  HeartbeatCore hb{};

  const bool seq_lf = hb.seq.is_lock_free();
  const bool ts_lf = hb.mono_ns.is_lock_free();
  std::printf("atomic<uint64_t> lock-free : seq=%s mono=%s\n",
              seq_lf ? "yes" : "NO", ts_lf ? "yes" : "NO");

  const auto mono = now_ns(CLOCK_MONOTONIC);
  const auto real = now_ns(CLOCK_REALTIME);
  std::printf("CLOCK_MONOTONIC : %llu ns\n", (unsigned long long)mono);
  std::printf("CLOCK_REALTIME  : %llu ns\n", (unsigned long long)real);
  std::printf("gap (real-mono) : %lld s (NTP/RTC effect; heartbeat uses MONOTONIC)\n",
              (long long)((real - mono) / 1000000000ll));

  if (!seq_lf || !ts_lf) {
    std::fprintf(stderr,
        "ERROR: 64-bit atomics are not lock-free. Likely a 32-bit image; "
        "the heartbeat protocol is invalid on this platform.\n");
    return 1;
  }
  std::puts("sanity OK");
  return 0;
}
