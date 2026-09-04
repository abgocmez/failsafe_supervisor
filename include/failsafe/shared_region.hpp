// Shared-memory region holding one heartbeat slot per worker. The supervisor
// creates and owns it; workers open the same named region and publish into
// their assigned slot. shm_open + ftruncate + mmap; the mapping stays valid and
// readable even after a writer process dies, which is exactly why the data
// plane alone cannot tell "dead" from "merely late" (see design notes).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "failsafe/heartbeat.hpp"

namespace failsafe {

inline constexpr std::uint32_t kRegionMagic = 0x46535550;  // "FSUP"
inline constexpr std::size_t kMaxSlots = 16;

// POD layout mapped identically into every process. Only trivially-copyable
// atomics, no pointers -- addresses differ per process, so nothing may store one.
struct SharedRegion {
  std::atomic<std::uint32_t> magic;       // kRegionMagic once initialized
  std::atomic<std::uint32_t> slot_count;  // number of slots in use
  HeartbeatSlot slots[kMaxSlots];
};

static_assert(std::is_trivially_copyable_v<SharedRegion>,
              "SharedRegion must be trivially copyable for shared mapping");

// Size passed to ftruncate; fixed so creator and openers agree.
inline constexpr std::size_t kRegionBytes = sizeof(SharedRegion);

}  // namespace failsafe
