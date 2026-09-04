#include "failsafe/safety_io.hpp"

#include <cstdio>

#if defined(HAVE_GPIO)
#include <atomic>
#include <chrono>
#include <thread>
#include <gpiod.hpp>
#endif

namespace failsafe {
namespace {

// Off-target backend: records ENABLE/heater and prints transitions. No button;
// the supervisor accepts SIGUSR1 as an alternate ack source for headless tests.
class LogSafetyIo final : public SafetyIo {
 public:
  void set_enable(bool on) override {
    if (on != enable_) { std::printf("[io] ENABLE=%s\n", on ? "on" : "off"); enable_ = on; }
  }
  void set_heater(double duty) override {
    const int pct = static_cast<int>(duty * 100.0 + 0.5);
    if (pct != heater_pct_) { heater_pct_ = pct; }  // no log spam for smooth ramps
  }
  bool ack_edge() override { return false; }  // no physical button off-target
 private:
  bool enable_ = false;
  int heater_pct_ = -1;
};

#if defined(HAVE_GPIO)

// Software PWM on one GPIO line: a thread toggles the line at a fixed carrier
// frequency according to an atomic duty. Its own chip request, so it never
// shares a line_request with the main thread.
class SoftPwm {
 public:
  explicit SoftPwm(unsigned line)
      : chip_("/dev/gpiochip0"),
        req_(chip_.prepare_request()
                 .set_consumer("failsafe-heater")
                 .add_line_settings(
                     line, gpiod::line_settings()
                               .set_direction(gpiod::line::direction::OUTPUT)
                               .set_output_value(gpiod::line::value::INACTIVE))
                 .do_request()),
        line_(line) {
    thread_ = std::thread([this] { run(); });
  }
  ~SoftPwm() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    req_.set_value(line_, gpiod::line::value::INACTIVE);
  }
  void set_duty(double d) {
    if (d < 0.0) d = 0.0; else if (d > 1.0) d = 1.0;
    duty_.store(d, std::memory_order_relaxed);
  }
 private:
  void run() {
    using namespace std::chrono;
    const auto period = microseconds(5000);  // 200 Hz carrier
    while (!stop_.load(std::memory_order_acquire)) {
      const double d = duty_.load(std::memory_order_relaxed);
      const auto on_time = duration_cast<microseconds>(period * d);
      if (on_time.count() > 0) {
        req_.set_value(line_, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(on_time);
      }
      const auto off_time = period - on_time;
      if (off_time.count() > 0) {
        req_.set_value(line_, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(off_time);
      }
    }
  }
  gpiod::chip chip_;
  gpiod::line_request req_;
  unsigned line_;
  std::atomic<double> duty_{0.0};
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

// Pi backend: ENABLE on GPIO17, heater PWM on GPIO18, ack button on GPIO27
// (input, internal pull-up; pressed = line pulled to GND = INACTIVE).
class GpioSafetyIo final : public SafetyIo {
 public:
  GpioSafetyIo()
      : chip_("/dev/gpiochip0"),
        req_(chip_.prepare_request()
                 .set_consumer("failsafe-safety")
                 .add_line_settings(
                     kEnable, gpiod::line_settings()
                                  .set_direction(gpiod::line::direction::OUTPUT)
                                  .set_output_value(gpiod::line::value::INACTIVE))
                 .add_line_settings(
                     kButton, gpiod::line_settings()
                                  .set_direction(gpiod::line::direction::INPUT)
                                  .set_bias(gpiod::line::bias::PULL_UP))
                 .do_request()),
        pwm_(kHeater) {}

  void set_enable(bool on) override {
    req_.set_value(kEnable, on ? gpiod::line::value::ACTIVE
                               : gpiod::line::value::INACTIVE);
  }
  void set_heater(double duty) override { pwm_.set_duty(duty); }

  bool ack_edge() override {
    const bool pressed =
        req_.get_value(kButton) == gpiod::line::value::INACTIVE;  // active-low
    const bool edge = pressed && !last_pressed_;
    last_pressed_ = pressed;
    return edge;
  }

 private:
  static constexpr unsigned kEnable = 17;
  static constexpr unsigned kHeater = 18;
  static constexpr unsigned kButton = 27;
  gpiod::chip chip_;
  gpiod::line_request req_;
  SoftPwm pwm_;
  bool last_pressed_ = false;
};

#endif  // HAVE_GPIO

}  // namespace

std::unique_ptr<SafetyIo> make_safety_io() {
#if defined(HAVE_GPIO)
  return std::make_unique<GpioSafetyIo>();
#else
  return std::make_unique<LogSafetyIo>();
#endif
}

}  // namespace failsafe
