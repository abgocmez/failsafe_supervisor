// Heartbeat slot: a lock-free single-producer/single-consumer record that a
// worker publishes and the supervisor reads. The pair (seq, mono_ns) is 16
// bytes, so it cannot be written with one atomic store; a naive multi-field
// read could tear (new seq, old timestamp). A seqlock gives a consistent read
// without a mutex -- deliberate, because a worker SIGKILLed mid-write must not
// leave a lock held. The writer never blocks.
//
// Sanitizer note: this seqlock uses std::atomic_thread_fence, which TSan does
// not model (-Wtsan), so a clean TSan run here under-validates rather than
// proves the protocol. ASan/UBSan do validate it and run in CI on x86_64
// (ASan aarch64 allocator does not start on the Pi 39-bit-VA kernel).
// Correctness rests on the C++ memory model, not on a sanitizer pass.
#pragma once
#include <atomic>
#include <cstdint>

namespace failsafe {

// One slot per worker. Lives in shared memory in M1 step 2; here it is exercised
// across two threads first to validate the protocol in isolation.
struct HeartbeatSlot {
  // Seqlock version. Even = stable, odd = a write is in progress.
  std::atomic<std::uint64_t> version{0};
  // Monotonically increasing heartbeat counter (for rate/"babbling" checks later).
  std::atomic<std::uint64_t> seq{0};
  // CLOCK_MONOTONIC timestamp of this beat, in nanoseconds (deadline tracking).
  std::atomic<std::uint64_t> mono_ns{0};

  // Producer side. Called by the worker once per heartbeat.
  void publish(std::uint64_t new_seq, std::uint64_t new_mono_ns) noexcept {
    const std::uint64_t v = version.load(std::memory_order_relaxed);
    version.store(v + 1, std::memory_order_relaxed);          // enter: now odd
    // Release fence: the odd-version store is ordered before the payload
    // stores, so a reader can never see even-version data mixed with a write.
    std::atomic_thread_fence(std::memory_order_release);
    seq.store(new_seq, std::memory_order_relaxed);
    mono_ns.store(new_mono_ns, std::memory_order_relaxed);
    // Release fence: payload stores are ordered before the even-version store.
    // Paired with the reader acquire-loading this even version, it makes the
    // payload visible whenever the reader accepts the read.
    std::atomic_thread_fence(std::memory_order_release);
    version.store(v + 2, std::memory_order_relaxed);          // leave: now even
  }

  // Consumer side. Returns false if a write was in progress or overlapped the
  // read (caller retries). On true, out_* hold a consistent snapshot.
  bool try_read(std::uint64_t& out_seq, std::uint64_t& out_mono_ns) const noexcept {
    const std::uint64_t v1 = version.load(std::memory_order_acquire);
    if (v1 & 1u) return false;                                // write in progress
    out_seq = seq.load(std::memory_order_relaxed);
    out_mono_ns = mono_ns.load(std::memory_order_relaxed);
    // Acquire fence: payload loads are ordered before re-reading the version,
    // so a concurrent write that started after v1 is detected by v1 != v2.
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t v2 = version.load(std::memory_order_relaxed);
    return v1 == v2;                                          // false = torn
  }
};

// A slot must be trivially copyable and contain only atomics so it can be
// placed directly in a shared-memory mapping (M1 step 2).
static_assert(sizeof(HeartbeatSlot) == 24, "unexpected slot layout");

}  // namespace failsafe
