// Failsafe supervisor (M1): spawns one worker, monitors its heartbeat against a
// deadline on a fixed timerfd tick, and drives the status output -- healthy
// while the beat is fresh, unhealthy once a deadline is missed. No restart yet
// (M2). Detection is purely deadline-based here; worst-case latency is
// deadline + tick, which is why both numbers are logged.
//   argv: supervisor [worker_path] [deadline_ms] [tick_ms] [period_ms]
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/mman.h>
#include <cerrno>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "failsafe/clock.hpp"
#include "failsafe/shared_memory.hpp"
#include "failsafe/status_output.hpp"

namespace {
volatile std::sig_atomic_t g_run = 1;
void on_term(int) { g_run = 0; }

std::string sibling_path(const char* name) {
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return name;
  buf[n] = 0;
  return std::string(::dirname(buf)) + "/" + name;
}

void arm_timer(int fd, long tick_ms) {
  const long ns = tick_ms * 1000000L;
  itimerspec its{};
  its.it_interval.tv_sec = ns / 1000000000L;
  its.it_interval.tv_nsec = ns % 1000000000L;
  its.it_value = its.it_interval;
  ::timerfd_settime(fd, 0, &its, nullptr);
}
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  const std::string worker_path = (argc > 1) ? argv[1] : sibling_path("worker");
  const std::uint64_t deadline_ms = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 100;
  const long tick_ms   = (argc > 3) ? std::strtol(argv[3], nullptr, 10) : 10;
  const long period_ms = (argc > 4) ? std::strtol(argv[4], nullptr, 10) : 100;
  const std::uint64_t deadline_ns = deadline_ms * 1000000ull;

  std::signal(SIGINT, on_term);
  std::signal(SIGTERM, on_term);

  const std::string shm_name = "/failsafe_" + std::to_string(::getpid());
  ::shm_unlink(shm_name.c_str());
  auto shm = failsafe::SharedMemory::create(shm_name);
  shm.region()->slot_count.store(1, std::memory_order_relaxed);
  auto& hb = shm.region()->slots[0];

  const pid_t child = ::fork();
  if (child < 0) { std::perror("fork"); return 2; }
  if (child == 0) {
    const std::string slot = "0";
    const std::string per = std::to_string(period_ms);
    ::execl(worker_path.c_str(), "worker", shm_name.c_str(), slot.c_str(),
            per.c_str(), static_cast<char*>(nullptr));
    std::perror("execl");
    _exit(127);
  }

  auto out = failsafe::make_status_output();
  const int tfd = ::timerfd_create(CLOCK_MONOTONIC, 0);
  if (tfd < 0) { std::perror("timerfd_create"); return 2; }
  arm_timer(tfd, tick_ms);

  std::printf("supervisor: worker pid=%d deadline=%llums tick=%ldms\n",
              child, (unsigned long long)deadline_ms, tick_ms);

  bool armed = false;      // becomes true after the first heartbeat is seen
  bool healthy = true;     // current asserted state
  std::uint64_t last_seq = 0, s = 0, ts = 0;

  while (g_run) {
    pollfd pfd{tfd, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, -1);
    if (pr < 0) { if (errno == EINTR) continue; break; }
    std::uint64_t expirations = 0;
    if (::read(tfd, &expirations, sizeof(expirations)) < 0 && errno == EINTR) continue;

    const std::uint64_t now = failsafe::now_mono_ns();
    bool fresh = false;
    if (hb.try_read(s, ts) && s > 0) {
      if (!armed) { armed = true; std::printf("supervisor: worker armed (first beat)\n"); }
      last_seq = s;
      fresh = (now - ts) <= deadline_ns;
    }

    const bool new_healthy = armed ? fresh : true;
    if (new_healthy != healthy) {
      const std::uint64_t age_ms = armed ? (now - ts) / 1000000ull : 0;
      std::printf("supervisor: healthy %s -> %s (seq=%llu age=%llums)\n",
                  healthy ? "true" : "false", new_healthy ? "true" : "false",
                  (unsigned long long)last_seq, (unsigned long long)age_ms);
      healthy = new_healthy;
    }
    out->set_healthy(healthy);
  }

  std::printf("supervisor: shutting down, stopping worker pid=%d\n", child);
  ::kill(child, SIGTERM);
  ::waitpid(child, nullptr, 0);
  out->set_healthy(false);
  ::close(tfd);
  return 0;
}
