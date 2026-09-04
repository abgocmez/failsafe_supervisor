cmake_minimum_required(VERSION 3.20)
project(failsafe_supervisor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- Sanitizer option (used by CI) ---
option(ENABLE_ASAN "AddressSanitizer + UBSan" OFF)
if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Hardware backend: OFF off-target (CI), which selects the no-op path ---
# BCM watchdog / gpiod are compiled only for the real Pi target, so the code
# base still builds on x86_64 CI where no such hardware exists.
option(HAVE_GPIO "Build the real GPIO backend using libgpiod" OFF)
if(HAVE_GPIO)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GPIODCXX REQUIRED IMPORTED_TARGET libgpiodcxx>=2.0)
  add_compile_definitions(HAVE_GPIO=1)
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
