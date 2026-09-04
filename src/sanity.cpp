// M0 sanity: heartbeat kaydinin lock-free olacagini ve saat kaynaklarini dogrular.
// Bu programin gectigi seyler, M1 heartbeat protokolunun on kosullaridir.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace {
// M1 heartbeat kaydinin cekirdegi: iki 64-bit atomik alan.
// aarch64 uzerinde bunlar native lock-free olmalidir. Degilse
// "lock-free" heartbeat iddiasi gecersizdir -> derlemede degil, calismada yakala.
struct HeartbeatCore {
  std::atomic<std::uint64_t> seq;
  std::atomic<std::uint64_t> mono_ns;
};

std::uint64_t now_ns(clockid_t clk) {
  timespec ts{};
  clock_gettime(clk, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}
}  // namespace

int main() {
  HeartbeatCore hb{};

  const bool seq_lf = hb.seq.is_lock_free();
  const bool ts_lf = hb.mono_ns.is_lock_free();
  std::printf("atomic<uint64_t> lock-free : seq=%s mono=%s\n",
              seq_lf ? "yes" : "NO", ts_lf ? "yes" : "NO");

  const auto mono = now_ns(CLOCK_MONOTONIC);
  const auto real = now_ns(CLOCK_REALTIME);
  std::printf("CLOCK_MONOTONIC : %llu ns\n", (unsigned long long)mono);
  std::printf("CLOCK_REALTIME  : %llu ns\n", (unsigned long long)real);
  std::printf("fark (real-mono): %lld s (NTP/RTC etkisi; heartbeat MONOTONIC kullanir)\n",
              (long long)((real - mono) / 1000000000ll));

  if (!seq_lf || !ts_lf) {
    std::fprintf(stderr,
        "HATA: 64-bit atomik lock-free degil. 32-bit imaj olabilir; "
        "heartbeat protokolu bu platformda gecersiz.\n");
    return 1;
  }
  std::puts("sanity OK");
  return 0;
}
