#include "failsafe/shared_memory.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>
#include <utility>

namespace failsafe {
namespace {
[[noreturn]] void throw_errno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}
}  // namespace

SharedMemory SharedMemory::create(const std::string& name) {
  // O_EXCL: fail if a stale region with this name already exists, so we never
  // silently adopt someone else layout. Caller unlinks first if reclaiming.
  int fd = ::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0) throw_errno("shm_open(create)");
  if (::ftruncate(fd, static_cast<off_t>(kRegionBytes)) != 0) {
    ::close(fd);
    ::shm_unlink(name.c_str());
    throw_errno("ftruncate");
  }
  void* addr = ::mmap(nullptr, kRegionBytes, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    ::close(fd);
    ::shm_unlink(name.c_str());
    throw_errno("mmap(create)");
  }
  auto* region = new (addr) SharedRegion();  // placement-new initializes atomics
  region->slot_count.store(0, std::memory_order_relaxed);
  region->magic.store(kRegionMagic, std::memory_order_release);  // publish last

  SharedMemory self;
  self.fd_ = fd;
  self.region_ = region;
  self.owner_ = true;
  self.name_ = name;
  return self;
}

SharedMemory SharedMemory::open(const std::string& name) {
  int fd = ::shm_open(name.c_str(), O_RDWR, 0600);
  if (fd < 0) throw_errno("shm_open(open)");
  void* addr = ::mmap(nullptr, kRegionBytes, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    ::close(fd);
    throw_errno("mmap(open)");
  }
  auto* region = reinterpret_cast<SharedRegion*>(addr);
  if (region->magic.load(std::memory_order_acquire) != kRegionMagic) {
    ::munmap(addr, kRegionBytes);
    ::close(fd);
    throw std::system_error(std::make_error_code(std::errc::bad_message),
                            "shared region magic mismatch");
  }

  SharedMemory self;
  self.fd_ = fd;
  self.region_ = region;
  self.owner_ = false;
  self.name_ = name;
  return self;
}

void SharedMemory::reset() noexcept {
  if (region_ != nullptr) {
    ::munmap(region_, kRegionBytes);
    region_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (owner_ && !name_.empty()) {
    ::shm_unlink(name_.c_str());
  }
  owner_ = false;
  name_.clear();
}

SharedMemory::~SharedMemory() { reset(); }

SharedMemory::SharedMemory(SharedMemory&& o) noexcept
    : fd_(o.fd_), region_(o.region_), owner_(o.owner_), name_(std::move(o.name_)) {
  o.fd_ = -1;
  o.region_ = nullptr;
  o.owner_ = false;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& o) noexcept {
  if (this != &o) {
    reset();
    fd_ = o.fd_;
    region_ = o.region_;
    owner_ = o.owner_;
    name_ = std::move(o.name_);
    o.fd_ = -1;
    o.region_ = nullptr;
    o.owner_ = false;
  }
  return *this;
}

}  // namespace failsafe
