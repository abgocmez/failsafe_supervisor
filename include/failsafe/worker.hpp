// Runtime state for one supervised worker: its spec (timing + restart policy)
// plus live state (pid, state-machine node, restart history for the budget).
// Per-worker state machine: Starting -> Alive -> Failed -> Restarting -> Alive,
// or -> GaveUp when the restart budget in the sliding window is spent.
#pragma once
#include <cstdint>
#include <deque>
#include <string>

namespace failsafe {

enum class WorkerState { Starting, Alive, Failed, Restarting, GaveUp };

inline const char* to_string(WorkerState s) {
  switch (s) {
    case WorkerState::Starting:   return "Starting";
    case WorkerState::Alive:      return "Alive";
    case WorkerState::Failed:     return "Failed";
    case WorkerState::Restarting: return "Restarting";
    case WorkerState::GaveUp:     return "GaveUp";
  }
  return "?";
}

// How a failure was detected -- the two paths whose latencies we contrast.
enum class DetectPath { None, Sigchld, Deadline };

struct WorkerSpec {
  std::string name;
  unsigned slot = 0;
  long period_ms = 100;                       // heartbeat cadence target
  std::uint64_t deadline_ns = 100000000ull;   // miss threshold (100 ms)
  int max_restarts = 3;                       // budget within the window
  std::uint64_t window_ns = 10000000000ull;   // 10 s sliding window
  // External workers are NOT spawned by the supervisor -- they attach to the
  // shared region from elsewhere (e.g. another container). They can only be
  // monitored via the deadline; they cannot be crash-detected (no SIGCHLD) or
  // restarted. A missed deadline therefore escalates straight to safe state.
  bool external = false;
};

struct Worker {
  WorkerSpec spec;

  pid_t pid = -1;
  WorkerState state = WorkerState::Starting;
  bool armed = false;                 // seen at least one beat since (re)start
  bool ever_armed = false;            // has ever attached (matters for external)
  std::uint64_t last_seq = 0;
  std::uint64_t spawn_mono_ns = 0;    // when the current instance was exec-ed

  std::deque<std::uint64_t> restart_times;  // monotonic ns of recent restarts
  int consecutive = 0;                // consecutive failures, drives backoff
  std::uint64_t next_restart_ns = 0;  // when Restarting: earliest re-exec time
};

}  // namespace failsafe
