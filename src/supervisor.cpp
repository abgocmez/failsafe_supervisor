// Failsafe supervisor (M2): supervises N workers with two detection paths and a
// budgeted restart policy.
//   * SIGCHLD (fast path): a worker we spawned dies -> the kernel signals us in
//     microseconds, folded into the poll loop via signalfd.
//   * Deadline (slow path): a worker is alive but silent (hang / SIGSTOP) -> we
//     find out only when its heartbeat ages past the deadline (~deadline + tick).
// On failure a worker is restarted with exponential backoff, up to a budget in a
// sliding window; when the budget is spent it enters GaveUp and the supervisor
// escalates (drives the status output unhealthy). The latched safe-state machine
// and the hardware watchdog come in M3/M4.
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "failsafe/clock.hpp"
#include "failsafe/shared_memory.hpp"
#include "failsafe/status_output.hpp"
#include "failsafe/worker.hpp"

namespace {

std::string g_worker_path;
std::string g_shm_name;

// Exponential backoff: 200 ms doubling per consecutive failure, capped at 3 s.
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

// Fork/exec one worker instance. Resets the inherited (blocked) signal mask in
// the child so the worker actually receives SIGTERM/SIGINT.
bool spawn(failsafe::Worker& w) {
  const pid_t pid = ::fork();
  if (pid < 0) { std::perror("fork"); return false; }
  if (pid == 0) {
    sigset_t empty;
    sigemptyset(&empty);
    ::sigprocmask(SIG_SETMASK, &empty, nullptr);
    const std::string slot = std::to_string(w.spec.slot);
    const std::string per = std::to_string(w.spec.period_ms);
    const std::string pname = "fsw-" + w.spec.name;  // distinct comm per worker
    ::execl(g_worker_path.c_str(), pname.c_str(), g_shm_name.c_str(), slot.c_str(),
            per.c_str(), pname.c_str(), static_cast<char*>(nullptr));
    std::perror("execl");
    _exit(127);
  }
  w.pid = pid;
  w.state = failsafe::WorkerState::Starting;
  w.armed = false;
  w.last_seq = 0;                       // new instance restarts its seq at 1
  w.spawn_mono_ns = failsafe::now_mono_ns();
  return true;
}

// Move a worker into Failed and decide restart vs give-up (budget in window).
void handle_failure(failsafe::Worker& w, failsafe::DetectPath path,
                    std::uint64_t now, std::uint64_t age_ns) {
  using failsafe::WorkerState;
  if (w.state == WorkerState::Failed || w.state == WorkerState::GaveUp ||
      w.state == WorkerState::Restarting) {
    return;  // already handled
  }
  // For SIGCHLD the metric is the crashed instance's lifetime; for the deadline
  // path it is the heartbeat age (the actual detection latency vs the deadline).
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
    std::printf("[policy] worker %s GaveUp: %d restarts within window spent -> ESCALATE\n",
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

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  const long tick_ms = (argc > 1) ? std::strtol(argv[1], nullptr, 10) : 10;

  g_worker_path = sibling_path("worker");

  std::vector<failsafe::Worker> workers(3);
  const char* names[3] = {"plant", "controller", "logger"};
  for (unsigned i = 0; i < 3; ++i) {
    workers[i].spec.name = names[i];
    workers[i].spec.slot = i;
    workers[i].spec.period_ms = 100;
    workers[i].spec.deadline_ns = 100000000ull;
    workers[i].spec.max_restarts = 3;
    workers[i].spec.window_ns = 10000000000ull;
  }

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  ::sigprocmask(SIG_BLOCK, &mask, nullptr);
  // SFD_NONBLOCK is essential: the drain loop below reads until EAGAIN. Without
  // it, the second read blocks until the next signal, stalling the whole event
  // loop (timerfd ticks stop, restarts never fire).
  const int sfd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sfd < 0) { std::perror("signalfd"); return 2; }

  g_shm_name = "/failsafe_" + std::to_string(::getpid());
  ::shm_unlink(g_shm_name.c_str());
  auto shm = failsafe::SharedMemory::create(g_shm_name);
  shm.region()->slot_count.store(workers.size(), std::memory_order_relaxed);

  for (auto& w : workers) spawn(w);

  auto out = failsafe::make_status_output();
  const int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (tfd < 0) { std::perror("timerfd_create"); return 2; }
  arm_timer(tfd, tick_ms);

  std::printf("supervisor: %zu workers, tick=%ldms\n", workers.size(), tick_ms);

  bool run = true;
  bool shutting_down = false;
  bool escalated = false;

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

      bool all_alive = true;
      for (const auto& w : workers) {
        if (w.state == failsafe::WorkerState::GaveUp) escalated = true;
        if (w.state != failsafe::WorkerState::Alive) all_alive = false;
      }
      out->set_healthy(all_alive && !escalated);
    }
  }

  std::printf("supervisor: shutting down\n");
  shutting_down = true;
  for (auto& w : workers) if (w.pid > 0) ::kill(w.pid, SIGTERM);
  for (auto& w : workers) if (w.pid > 0) ::waitpid(w.pid, nullptr, 0);
  out->set_healthy(false);
  ::close(tfd);
  ::close(sfd);
  return 0;
}
