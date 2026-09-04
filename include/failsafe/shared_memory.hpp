// RAII wrapper around a POSIX shared-memory object mapping a SharedRegion.
// create(): supervisor side -- O_CREAT|O_EXCL, sizes it, maps it, writes magic.
// open():   worker side     -- opens the existing object and maps it.
// The creator unlinks the name on destruction; every holder munmaps its view.
#pragma once
#include <string>
#include "failsafe/shared_region.hpp"

namespace failsafe {

class SharedMemory {
 public:
  static SharedMemory create(const std::string& name);
  static SharedMemory open(const std::string& name);

  SharedMemory(SharedMemory&& other) noexcept;
  SharedMemory& operator=(SharedMemory&& other) noexcept;
  SharedMemory(const SharedMemory&) = delete;
  SharedMemory& operator=(const SharedMemory&) = delete;
  ~SharedMemory();

  SharedRegion* region() const noexcept { return region_; }
  bool is_owner() const noexcept { return owner_; }

 private:
  SharedMemory() = default;
  void reset() noexcept;

  int fd_ = -1;
  SharedRegion* region_ = nullptr;
  bool owner_ = false;
  std::string name_;
};

}  // namespace failsafe
