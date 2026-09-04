// M0: LED blink via libgpiod 2.x. Proves the hardware backend (HAVE_GPIO)
// compiles and that a GPIO line can be driven.
// LED: GPIO17 (physical pin 11) -> 330R -> LED -> GND.
#include <atomic>
#include <csignal>
#include <cstdio>
#include <thread>
#include <gpiod.hpp>

namespace {
constexpr unsigned kLedLine = 17;  // BCM GPIO17 (physical pin 11)
constexpr auto kHalfPeriod = std::chrono::milliseconds(500);
std::atomic<bool> g_run{true};
void on_signal(int) { g_run.store(false); }
}  // namespace

int main() {
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  gpiod::chip chip("/dev/gpiochip0");
  auto request = chip.prepare_request()
      .set_consumer("failsafe-blink")
      .add_line_settings(
          kLedLine,
          gpiod::line_settings()
              .set_direction(gpiod::line::direction::OUTPUT)
              .set_output_value(gpiod::line::value::INACTIVE))
      .do_request();

  std::puts("blink: GPIO17 toggling (Ctrl-C to exit)");
  bool on = false;
  while (g_run.load()) {
    on = !on;
    request.set_value(kLedLine,
        on ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(kHalfPeriod);
  }
  // Clean shutdown: drive the LED off (a miniature of the safe-state reflex).
  request.set_value(kLedLine, gpiod::line::value::INACTIVE);
  std::puts("\nblink: LED off, exiting");
  return 0;
}
