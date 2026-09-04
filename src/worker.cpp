// A supervised worker (M1/M2: well-behaved only). It opens the shared region,
// takes its slot index, and publishes a heartbeat every period using absolute
// CLOCK_MONOTONIC wakeups (clock_nanosleep TIMER_ABSTIME) so the cadence does
// not drift. Later milestones add misbehaviour modes (stall, crash, babble).
//   argv: worker <shm_name> <slot_index> [period_ms] [name]
#include <sys/prctl.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "failsafe/clock.hpp"
#include "failsafe/shared_memory.hpp"

namespace {
volatile std::sig_atomic_t g_run = 1;
volatile std::sig_atomic_t g_stall = 0;   // timing-fault injection (late heartbeat)
void on_term(int) { g_run = 0; }
void on_stall(int) { g_stall = 1; }
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <shm_name> <slot_index> [period_ms] [name]\n", argv[0]);
    return 2;
  }
  const std::string shm_name = argv[1];
  const unsigned slot = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
  const long period_ms = (argc > 3) ? std::strtol(argv[3], nullptr, 10) : 100;

  // Distinct process name so tools (ps/top/pgrep -x) can target one worker.
  if (argc > 4) ::prctl(PR_SET_NAME, argv[4], 0, 0, 0);

  std::signal(SIGTERM, on_term);
  std::signal(SIGINT, on_term);
  std::signal(SIGUSR1, on_stall);  // inject a transient heartbeat stall

  // If the supervisor dies, the kernel sends us SIGTERM so we do not linger as
  // an orphan. Re-check in case the parent already died before this call.
  ::prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
  if (::getppid() == 1) return 0;

  auto shm = failsafe::SharedMemory::open(shm_name);
  auto& hb = shm.region()->slots[slot];

  const long period_ns = period_ms * 1000000L;
  timespec next{};
  ::clock_gettime(CLOCK_MONOTONIC, &next);

  std::uint64_t seq = 0;
  while (g_run) {
    if (g_stall) {
      // Timing fault: stop publishing for 300 ms (> deadline), then resume.
      g_stall = 0;
      timespec st{};
      st.tv_nsec = 300000000L;
      ::nanosleep(&st, nullptr);
      ::clock_gettime(CLOCK_MONOTONIC, &next);  // re-anchor the cadence
      continue;
    }
    hb.publish(++seq, failsafe::now_mono_ns());

    next.tv_nsec += period_ns;
    while (next.tv_nsec >= 1000000000L) {
      next.tv_nsec -= 1000000000L;
      next.tv_sec += 1;
    }
    // Absolute sleep: no accumulating drift, and it re-targets even if a beat
    // was late. EINTR (a signal) just falls through to the g_run check.
    ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
  }
  std::printf("worker[slot %u]: exiting after %llu beats\n", slot,
              (unsigned long long)seq);
  return 0;
}
