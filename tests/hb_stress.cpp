// Standalone stress test for the seqlock heartbeat protocol (M1 step 1).
// Not yet shared memory -- two threads sharing one slot -- but it validates
// exactly the cross-context ordering the SHM version will rely on, and it is
// the concurrency TSan cannot check once the writer becomes a separate process.
//
// Trick: the writer sets mono_ns = mix(seq), a bijective scramble of seq. Any
// torn read (new seq with stale mono_ns, or vice versa) breaks mix(seq)==mono_ns
// with overwhelming probability, so the invariant check catches tearing directly.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include "failsafe/heartbeat.hpp"

namespace {
constexpr std::uint64_t kIterations = 5000000;

// Bijective mix so field staleness is detectable.
std::uint64_t mix(std::uint64_t x) noexcept {
  const std::uint64_t rot = (x << 32) | (x >> 32);
  return rot ^ (x * 0x9E3779B97F4A7C15ull);
}
}  // namespace

int main() {
  failsafe::HeartbeatSlot slot;
  std::atomic<bool> done{false};

  std::thread writer([&] {
    for (std::uint64_t i = 1; i <= kIterations; ++i) {
      slot.publish(i, mix(i));
    }
    done.store(true, std::memory_order_release);
  });

  std::uint64_t reads = 0, retries = 0, torn = 0, backward = 0;
  std::uint64_t last_seq = 0, s = 0, p = 0;
  while (!done.load(std::memory_order_acquire)) {
    if (slot.try_read(s, p)) {
      ++reads;
      if (s != 0 && mix(s) != p) ++torn;       // must never happen
      if (s < last_seq) ++backward;            // seq must not go backwards
      last_seq = s;
    } else {
      ++retries;
    }
  }
  writer.join();

  std::printf("reads=%llu retries=%llu final_seq=%llu torn=%llu backward=%llu\n",
              (unsigned long long)reads, (unsigned long long)retries,
              (unsigned long long)last_seq, (unsigned long long)torn,
              (unsigned long long)backward);

  if (torn != 0 || backward != 0) {
    std::fprintf(stderr, "FAIL: seqlock violated (torn=%llu backward=%llu)\n",
                 (unsigned long long)torn, (unsigned long long)backward);
    return 1;
  }
  std::puts("hb_stress OK");
  return 0;
}
