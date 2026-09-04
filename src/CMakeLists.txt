add_executable(sanity sanity.cpp)

# IPC library: shared-memory management (POSIX shm_open/mmap).
add_library(failsafe_ipc STATIC shared_memory.cpp)
target_link_libraries(failsafe_ipc PUBLIC failsafe_headers)
# shm_open lives in libc on glibc >= 2.34; link librt when a separate one exists.
find_library(RT_LIB rt)
if(RT_LIB)
  target_link_libraries(failsafe_ipc PUBLIC ${RT_LIB})
endif()

# Hardware-dependent targets are built only on the Pi (HAVE_GPIO=ON).
if(HAVE_GPIO)
  add_executable(blink blink.cpp)
  target_link_libraries(blink PRIVATE PkgConfig::GPIODCXX)
endif()
