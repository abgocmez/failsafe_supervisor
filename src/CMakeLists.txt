add_executable(sanity sanity.cpp)

# Hardware-dependent targets are built only on the Pi (HAVE_GPIO=ON).
# CI (x86_64, HAVE_GPIO=OFF) skips them; the code base still compiles.
if(HAVE_GPIO)
  add_executable(blink blink.cpp)
  target_link_libraries(blink PRIVATE PkgConfig::GPIODCXX)
endif()
