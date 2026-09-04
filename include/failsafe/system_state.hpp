// System-level state machine, owned solely by the supervisor. Distinct from the
// per-worker state machine: this one decides the plant's safety posture.
//   Init      -- starting up, workers not yet all alive
//   Running   -- all workers alive; ENABLE on, heater active
//   Degraded  -- a worker is failed/restarting but recovery is still in budget;
//                ENABLE on, heater reduced
//   SafeState -- a worker gave up (unrecoverable): heater 0, ENABLE off. LATCHED:
//                the system stays here until an explicit ack (button or SIGUSR1).
// Every transition is logged with a CLOCK_MONOTONIC timestamp; that log is the
// measurement record for M5.
#pragma once

namespace failsafe {

enum class SystemState { Init, Running, Degraded, SafeState };

inline const char* to_string(SystemState s) {
  switch (s) {
    case SystemState::Init:      return "Init";
    case SystemState::Running:   return "Running";
    case SystemState::Degraded:  return "Degraded";
    case SystemState::SafeState: return "SafeState";
  }
  return "?";
}

}  // namespace failsafe
