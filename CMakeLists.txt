cmake_minimum_required(VERSION 3.20)
project(failsafe_supervisor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- Sanitizer secenegi (CI kullanir) ---
option(ENABLE_ASAN "AddressSanitizer + UBSan" OFF)
if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Donanim arayuzu: Pi disinda (CI) OFF, no-op backend ---
# BCM watchdog / gpiod yalnizca gercek Pi hedefinde derlenir.
# Boylece kod x86_64 CI uzerinde de derlenebilir.
option(HAVE_GPIO "libgpiod ile gercek GPIO backendini derle" OFF)
if(HAVE_GPIO)
  find_library(GPIOD_LIB gpiod REQUIRED)
  add_compile_definitions(HAVE_GPIO=1)
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
