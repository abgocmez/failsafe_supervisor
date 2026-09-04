#include "failsafe/status_output.hpp"

#include <cstdio>

#if defined(HAVE_GPIO)
#include <gpiod.hpp>
#endif

namespace failsafe {
namespace {

// Off-target backend (CI, or any non-Pi build): logs transitions only.
class LogOutput final : public StatusOutput {
 public:
  void set_healthy(bool healthy) override {
    if (healthy != last_) {
      std::printf("[status] healthy=%s\n", healthy ? "true" : "false");
      last_ = healthy;
    }
  }
 private:
  bool last_ = true;
};

#if defined(HAVE_GPIO)
// Pi backend: drives an LED on GPIO17. on = healthy, off = deadline missed.
class GpioOutput final : public StatusOutput {
 public:
  GpioOutput()
      : request_(gpiod::chip("/dev/gpiochip0")
                     .prepare_request()
                     .set_consumer("failsafe-status")
                     .add_line_settings(
                         kLine,
                         gpiod::line_settings()
                             .set_direction(gpiod::line::direction::OUTPUT)
                             .set_output_value(gpiod::line::value::ACTIVE))
                     .do_request()) {}

  void set_healthy(bool healthy) override {
    request_.set_value(kLine, healthy ? gpiod::line::value::ACTIVE
                                      : gpiod::line::value::INACTIVE);
  }
 private:
  static constexpr unsigned kLine = 17;  // BCM GPIO17 (physical pin 11)
  gpiod::line_request request_;
};
#endif

}  // namespace

std::unique_ptr<StatusOutput> make_status_output() {
#if defined(HAVE_GPIO)
  return std::make_unique<GpioOutput>();
#else
  return std::make_unique<LogOutput>();
#endif
}

}  // namespace failsafe
