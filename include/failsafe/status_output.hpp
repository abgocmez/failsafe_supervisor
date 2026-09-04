// Thin hardware-output interface: the supervisor asserts "system healthy" here
// and the backend drives it onto real hardware or a no-op sink. Kept to a tiny
// surface on purpose -- it is the only seam between portable logic and the Pi,
// so the whole code base builds on x86_64 CI (HAVE_GPIO=OFF -> LogOutput).
// In M3 this grows the heater PWM and latched ENABLE line; for M1 it is one bit.
#pragma once
#include <memory>

namespace failsafe {

class StatusOutput {
 public:
  virtual ~StatusOutput() = default;
  // true  = system believes all workers alive (LED on / heater permitted)
  // false = a deadline was missed (LED off) -- the visible M1 result.
  virtual void set_healthy(bool healthy) = 0;
};

// Factory: returns the GPIO-backed output when built with HAVE_GPIO, otherwise
// a logging no-op so the same code runs off-target.
std::unique_ptr<StatusOutput> make_status_output();

}  // namespace failsafe
