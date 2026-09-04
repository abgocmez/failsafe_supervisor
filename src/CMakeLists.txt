add_executable(sanity sanity.cpp)

# IPC library: POSIX shared-memory management.
add_library(failsafe_ipc STATIC shared_memory.cpp)
target_link_libraries(failsafe_ipc PUBLIC failsafe_headers)
find_library(RT_LIB rt)
if(RT_LIB)
  target_link_libraries(failsafe_ipc PUBLIC ${RT_LIB})
endif()

# Safety-I/O library: real GPIO backend (ENABLE + heater PWM + ack button) on
# the Pi, logging no-op off-target. Needs threads for the software PWM.
add_library(failsafe_hw STATIC safety_io.cpp)
target_link_libraries(failsafe_hw PUBLIC failsafe_headers Threads::Threads)
if(HAVE_GPIO)
  target_link_libraries(failsafe_hw PRIVATE PkgConfig::GPIODCXX)
endif()

# Worker and supervisor executables.
add_executable(worker worker.cpp)
target_link_libraries(worker PRIVATE failsafe_ipc)

add_executable(supervisor supervisor.cpp)
target_link_libraries(supervisor PRIVATE failsafe_ipc failsafe_hw)

# Standalone LED blink (M0), Pi only.
if(HAVE_GPIO)
  add_executable(blink blink.cpp)
  target_link_libraries(blink PRIVATE PkgConfig::GPIODCXX)
endif()
