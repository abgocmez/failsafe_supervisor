// Safety I/O: the hardware seam the supervisor drives. The actuator is owned by
// the supervisor (single point of actuation), so a failed worker can never move
// the output directly -- it can only cause the supervisor to command a state.
//   * ENABLE line  -- active-high permission to operate; dropped in safe state.
//   * heater PWM   -- the process actuator; duty 0..1 (LED brightness). 0 in safe state.
//   * ack button   -- clears the latched safe state (edge-detected, one-shot).
// GPIO backend on the Pi (ENABLE=GPIO17, heater=GPIO18, button=GPIO27), a
// logging no-op off-target so the same logic runs and is tested in CI.
#pragma once
#include <memory>

namespace failsafe {

class SafetyIo {
 public:
  virtual ~SafetyIo() = default;
  virtual void set_enable(bool on) = 0;      // ENABLE line
  virtual void set_heater(double duty) = 0;  // 0..1 PWM duty
  virtual bool ack_edge() = 0;               // true once per button press
};

std::unique_ptr<SafetyIo> make_safety_io();

}  // namespace failsafe
