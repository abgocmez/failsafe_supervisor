// RAII wrapper for the Linux hardware watchdog (/dev/watchdog). The supervisor
// kicks it every tick while its event loop is alive; if the supervisor hangs or
// dies, the kicks stop and the BCM watchdog reboots the Pi after the timeout --
// a reset drives every GPIO back to input (ENABLE falls, heater off), so a dead
// supervisor still converges on the safe state. This is who-watches-the-watcher.
//
// Opening is best-effort: if the device is missing or we lack privilege (it is
// root-only), the supervisor logs and runs on without hardware protection, so
// non-root runs and CI still work. A clean shutdown writes the magic character
// 'V' before close to disarm, so stopping the supervisor normally never reboots.
#pragma once
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/watchdog.h>

#include <cstdio>

namespace failsafe {

class Watchdog {
 public:
  explicit Watchdog(const char* dev = "/dev/watchdog0", int want_timeout_s = 15) {
    fd_ = ::open(dev, O_WRONLY | O_CLOEXEC);
    if (fd_ < 0) {
      std::printf("[wdog] unavailable (%s) -- running without hardware watchdog\n", dev);
      return;
    }
    int t = want_timeout_s;
    ::ioctl(fd_, WDIOC_SETTIMEOUT, &t);      // may be unsupported; ignore result
    ::ioctl(fd_, WDIOC_GETTIMEOUT, &timeout_);
    std::printf("[wdog] armed on %s, timeout=%ds (kicked every tick)\n", dev, timeout_);
  }

  ~Watchdog() {
    if (fd_ < 0) return;
    // Magic close: disarm so a clean shutdown does not reboot the box.
    (void)!::write(fd_, "V", 1);
    ::close(fd_);
  }

  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;

  void kick() {
    if (fd_ >= 0) ::ioctl(fd_, WDIOC_KEEPALIVE, 0);
  }

  bool armed() const { return fd_ >= 0; }
  int timeout_s() const { return timeout_; }

 private:
  int fd_ = -1;
  int timeout_ = 0;
};

}  // namespace failsafe
