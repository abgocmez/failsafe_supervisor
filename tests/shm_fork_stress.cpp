// M1 step 2: the seqlock heartbeat exercised across a real process boundary
// over POSIX shared memory. Parent creates the region and forks; the child
// opens the same region and publishes into slot 0; the parent reads and checks
// the same invariants as hb_stress (no torn read, seq never goes backwards).
// This is the cross-process SPSC that TSan cannot see -- validated by construction.
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include "failsafe/shared_memory.hpp"

namespace {
constexpr std::uint64_t kIterations = 2000000;
std::uint64_t mix(std::uint64_t x) noexcept {
  const std::uint64_t rot = (x << 32) | (x >> 32);
  return rot ^ (x * 0x9E3779B97F4A7C15ull);
}
}  // namespace

int main() {
  const std::string name = "/failsafe_test_" + std::to_string(::getpid());
  ::shm_unlink(name.c_str());  // clear any stale region from a crashed run

  auto shm = failsafe::SharedMemory::create(name);
  auto* region = shm.region();
  region->slot_count.store(1, std::memory_order_relaxed);
  auto& slot = region->slots[0];

  const pid_t pid = ::fork();
  if (pid < 0) {
    std::perror("fork");
    return 2;
  }

  if (pid == 0) {
    // Child = worker. Open the same region and publish.
    auto child_shm = failsafe::SharedMemory::open(name);
    auto& child_slot = child_shm.region()->slots[0];
    for (std::uint64_t i = 1; i <= kIterations; ++i) {
      child_slot.publish(i, mix(i));
    }
    _exit(0);
  }

  // Parent = supervisor. Read and validate until the child is done.
  std::uint64_t reads = 0, retries = 0, torn = 0, backward = 0;
  std::uint64_t last_seq = 0, s = 0, p = 0;
  bool child_done = false;
  while (!child_done || last_seq < kIterations) {
    if (slot.try_read(s, p)) {
      ++reads;
      if (s != 0 && mix(s) != p) ++torn;
      if (s < last_seq) ++backward;
      last_seq = s;
    } else {
      ++retries;
    }
    int status = 0;
    if (!child_done && ::waitpid(pid, &status, WNOHANG) == pid) {
      child_done = true;  // one more drain pass rounds out to final seq
    }
  }

  std::printf("reads=%llu retries=%llu final_seq=%llu torn=%llu backward=%llu\n",
              (unsigned long long)reads, (unsigned long long)retries,
              (unsigned long long)last_seq, (unsigned long long)torn,
              (unsigned long long)backward);

  if (torn != 0 || backward != 0 || last_seq != kIterations) {
    std::fprintf(stderr, "FAIL: torn=%llu backward=%llu final_seq=%llu\n",
                 (unsigned long long)torn, (unsigned long long)backward,
                 (unsigned long long)last_seq);
    return 1;
  }
  std::puts("shm_fork_stress OK");
  return 0;
}
