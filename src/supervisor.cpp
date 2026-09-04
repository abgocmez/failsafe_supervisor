// Failsafe supervisor (M3): M2 plus the system-level state machine and a real,
// latched safe state driven onto the safety I/O.
//   * SIGCHLD fast path + deadline slow path detection (M2).
//   * Budgeted restart with exponential backoff (M2).
//   * System state machine Init/Running/Degraded/SafeState. A worker that gives
//     up drives the system to SafeState: heater 0, ENABLE off, LATCHED until an
//     explicit ack (physical button or SIGUSR1). Every transition is logged with
//     a monotonic timestamp -- the M5 measurement record.
//   argv: supervisor [tick_ms]
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "failsafe/clock.hpp"
#include "failsafe/safety_io.hpp"
#include "failsafe/shared_memory.hpp"
#include "failsafe/system_state.hpp"
#include "failsafe/watchdog.hpp"
#include "failsafe/worker.hpp"

namespace {

std::string g_worker_path;
std::string g_shm_name;

std::uint64_t backoff_ns(int consecutive) {
  const std::uint64_t base = 200000000ull;   // 200 ms
  const std::uint64_t cap = 3000000000ull;   // 3 s
  std::uint64_t d = base;
  for (int i = 0; i < consecutive && d < cap; ++i) d <<= 1;
  return d < cap ? d : cap;
}

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

bool spawn(failsafe::Worker& w) {
  const pid_t pid = ::fork();
  if (pid < 0) { std::perror("fork"); return false; }
  if (pid == 0) {
    sigset_t empty;
    sigemptyset(&empty);
    ::sigprocmask(SIG_SETMASK, &empty, nullptr);
    const std::string slot = std::to_string(w.spec.slot);
    const std::string per = std::to_string(w.spec.period_ms);
    const std::string pname = "fsw-" + w.spec.name;
    ::execl(g_worker_path.c_str(), pname.c_str(), g_shm_name.c_str(), slot.c_str(),
            per.c_str(), pname.c_str(), static_cast<char*>(nullptr));
    std::perror("execl");
    _exit(127);
  }
  w.pid = pid;
  w.state = failsafe::WorkerState::Starting;
  w.armed = false;
  w.last_seq = 0;
  w.spawn_mono_ns = failsafe::now_mono_ns();
  return true;
}

void handle_failure(failsafe::Worker& w, failsafe::DetectPath path,
                    std::uint64_t now, std::uint64_t age_ns) {
  using failsafe::WorkerState;
  if (w.state == WorkerState::Failed || w.state == WorkerState::GaveUp ||
      w.state == WorkerState::Restarting) {
    return;
  }
  // Structured event (monotonic-ns) for the measurement harness.
  std::printf("EV %llu %s %s\n", (unsigned long long)now,
              path == failsafe::DetectPath::Sigchld ? "DETECT_SIGCHLD" : "DETECT_DEADLINE",
              w.spec.name.c_str());
  if (path == failsafe::DetectPath::Sigchld) {
    std::printf("[detect] worker %s failed via SIGCHLD (fast) (lived=%llums seq=%llu)\n",
                w.spec.name.c_str(), (unsigned long long)(age_ns / 1000000ull),
                (unsigned long long)w.last_seq);
  } else {
    std::printf("[detect] worker %s failed via deadline (slow) (age=%llums seq=%llu)\n",
                w.spec.name.c_str(), (unsigned long long)(age_ns / 1000000ull),
                (unsigned long long)w.last_seq);
  }
  w.state = WorkerState::Failed;
  w.armed = false;

  while (!w.restart_times.empty() &&
         now - w.restart_times.front() > w.spec.window_ns) {
    w.restart_times.pop_front();
  }
  if (static_cast<int>(w.restart_times.size()) >= w.spec.max_restarts) {
    w.state = WorkerState::GaveUp;
    std::printf("EV %llu GAVEUP %s\n", (unsigned long long)now, w.spec.name.c_str());
    std::printf("[policy] worker %s GaveUp: %d restarts within window spent\n",
                w.spec.name.c_str(), w.spec.max_restarts);
    return;
  }
  w.restart_times.push_back(now);
  const std::uint64_t delay = backoff_ns(w.consecutive);
  w.next_restart_ns = now + delay;
  w.consecutive += 1;
  w.state = WorkerState::Restarting;
  std::printf("[policy] worker %s restart #%d scheduled in %llums (backoff)\n",
              w.spec.name.c_str(), static_cast<int>(w.restart_times.size()),
              (unsigned long long)(delay / 1000000ull));
}

// Heater duty while Running: a slow breathe so the LED visibly "runs".
double breathe(std::uint64_t now) {
  const double t = static_cast<double>(now) / 1e9;
  const double s = (std::sin(t * 2.0 * 3.14159265 * 0.3) + 1.0) * 0.5;  // 0.3 Hz
  return 0.1 + 0.9 * s;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  const long tick_ms = (argc > 1) ? std::strtol(argv[1], nullptr, 10) : 10;

  g_worker_path = sibling_path("worker");

  // Restart budget; overridable so the measurement harness can run many
  // injections without tripping escalation (latency measurement, not policy).
  const char* mr_env = std::getenv("FAILSAFE_MAX_RESTARTS");
  const int max_restarts = mr_env ? std::atoi(mr_env) : 3;

  std::vector<failsafe::Worker> workers(3);
  const char* names[3] = {"plant", "controller", "logger"};
  for (unsigned i = 0; i < 3; ++i) {
    workers[i].spec.name = names[i];
    workers[i].spec.slot = i;
    workers[i].spec.period_ms = 100;
    workers[i].spec.deadline_ns = 100000000ull;
    workers[i].spec.max_restarts = max_restarts;
    workers[i].spec.window_ns = 10000000000ull;
  }

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGUSR1);  // alternate ack source (headless / CI)
  ::sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sfd < 0) { std::perror("signalfd"); return 2; }

  g_shm_name = "/failsafe_" + std::to_string(::getpid());
  ::shm_unlink(g_shm_name.c_str());
  auto shm = failsafe::SharedMemory::create(g_shm_name);
  shm.region()->slot_count.store(workers.size(), std::memory_order_relaxed);

  for (auto& w : workers) spawn(w);

  auto io = failsafe::make_safety_io();
  const int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (tfd < 0) { std::perror("timerfd_create"); return 2; }
  arm_timer(tfd, tick_ms);

  // Hardware watchdog: kicked every tick while the event loop lives. If the
  // supervisor hangs, the kicks stop and the Pi reboots (safe by construction).
  failsafe::Watchdog wdog;

  std::printf("supervisor: %zu workers, tick=%ldms\n", workers.size(), tick_ms);

  bool run = true;
  bool shutting_down = false;
  bool ack_pending = false;
  failsafe::SystemState state = failsafe::SystemState::Init;
  std::uint64_t safe_entered_ns = 0;

  auto find_by_pid = [&](pid_t pid) -> failsafe::Worker* {
    for (auto& w : workers) if (w.pid == pid) return &w;
    return nullptr;
  };

  while (run) {
    pollfd pfds[2] = {{sfd, POLLIN, 0}, {tfd, POLLIN, 0}};
    const int pr = ::poll(pfds, 2, -1);
    if (pr < 0) { if (errno == EINTR) continue; break; }

    if (pfds[0].revents & POLLIN) {
      signalfd_siginfo si;
      while (::read(sfd, &si, sizeof(si)) == sizeof(si)) {
        if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
          run = false;
        } else if (si.ssi_signo == SIGUSR1) {
          ack_pending = true;
        } else if (si.ssi_signo == SIGCHLD) {
          int status = 0;
          pid_t pid;
          while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) {
            failsafe::Worker* w = find_by_pid(pid);
            if (w == nullptr || shutting_down) continue;
            const std::uint64_t now = failsafe::now_mono_ns();
            handle_failure(*w, failsafe::DetectPath::Sigchld, now,
                           now - w->spawn_mono_ns);
          }
        }
      }
      if (!run) break;
    }

    if (pfds[1].revents & POLLIN) {
      std::uint64_t exp = 0;
      if (::read(tfd, &exp, sizeof(exp)) < 0 && errno == EINTR) continue;
      wdog.kick();  // event loop is alive this tick -> pet the hardware watchdog
      const std::uint64_t now = failsafe::now_mono_ns();

      for (auto& w : workers) {
        std::uint64_t s = 0, ts = 0;
        if (w.state != failsafe::WorkerState::Restarting &&
            w.state != failsafe::WorkerState::GaveUp &&
            shm.region()->slots[w.spec.slot].try_read(s, ts) && s > 0) {
          if (!w.armed) {
            w.armed = true;
            w.consecutive = 0;
            if (w.state == failsafe::WorkerState::Starting) {
              w.state = failsafe::WorkerState::Alive;
              std::printf("EV %llu ALIVE %s\n", (unsigned long long)now, w.spec.name.c_str());
              std::printf("[state] worker %s -> Alive\n", w.spec.name.c_str());
            }
          }
          w.last_seq = s;
          if (w.state == failsafe::WorkerState::Alive &&
              (now - ts) > w.spec.deadline_ns) {
            handle_failure(w, failsafe::DetectPath::Deadline, now, now - ts);
          }
        }

        if (w.state == failsafe::WorkerState::Restarting &&
            now >= w.next_restart_ns) {
          std::printf("[state] worker %s -> Restarting (exec)\n",
                      w.spec.name.c_str());
          spawn(w);
        }
      }

      // Aggregate worker view.
      bool any_gaveup = false, all_alive = true;
      for (const auto& w : workers) {
        if (w.state == failsafe::WorkerState::GaveUp) any_gaveup = true;
        if (w.state != failsafe::WorkerState::Alive) all_alive = false;
      }

      // Ack from either source, consumed once.
      const bool ack = ack_pending || io->ack_edge();
      ack_pending = false;

      // --- System state machine ---
      const failsafe::SystemState prev = state;
      if (state != failsafe::SystemState::SafeState) {
        if (any_gaveup) {
          state = failsafe::SystemState::SafeState;
          safe_entered_ns = now;
        } else if (all_alive) {
          state = failsafe::SystemState::Running;
        } else {
          state = failsafe::SystemState::Degraded;
        }
      } else if (ack) {
        // Latched safe state cleared by a human. Re-arm the workers that gave up.
        for (auto& w : workers) {
          if (w.state == failsafe::WorkerState::GaveUp) {
            w.restart_times.clear();
            w.consecutive = 0;
            spawn(w);
          }
        }
        std::printf("[ack] safe state cleared after %llums, re-arming workers\n",
                    (unsigned long long)((now - safe_entered_ns) / 1000000ull));
        state = failsafe::SystemState::Init;
      }

      if (state != prev) {
        std::printf("[SYSTEM] %s -> %s (t=%llums)\n", failsafe::to_string(prev),
                    failsafe::to_string(state),
                    (unsigned long long)(now / 1000000ull));
        if (state == failsafe::SystemState::SafeState) {
          std::printf("EV %llu SAFESTATE -\n", (unsigned long long)now);
          std::printf("[SAFE] heater=0 ENABLE=off latched (ack to clear)\n");
        }
      }

      // --- Drive the safety I/O from the state ---
      switch (state) {
        case failsafe::SystemState::Running:
          io->set_enable(true);
          io->set_heater(breathe(now));
          break;
        case failsafe::SystemState::Degraded:
          io->set_enable(true);
          io->set_heater(0.15);  // reduced while recovering
          break;
        case failsafe::SystemState::Init:
          io->set_enable(true);
          io->set_heater(0.0);
          break;
        case failsafe::SystemState::SafeState:
          io->set_enable(false);
          io->set_heater(0.0);
          break;
      }
    }
  }

  std::printf("supervisor: shutting down\n");
  shutting_down = true;
  for (auto& w : workers) if (w.pid > 0) ::kill(w.pid, SIGTERM);
  for (auto& w : workers) if (w.pid > 0) ::waitpid(w.pid, nullptr, 0);
  io->set_enable(false);
  io->set_heater(0.0);
  ::close(tfd);
  ::close(sfd);
  return 0;
}
