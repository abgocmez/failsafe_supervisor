# M0: a plain CTest test proving the CI pipeline end to end.
add_test(NAME sanity_runs COMMAND sanity)
set_tests_properties(sanity_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "sanity OK")

# M1 step 1: seqlock heartbeat stress test (portable; runs in CI too).
add_executable(hb_stress hb_stress.cpp)
target_link_libraries(hb_stress PRIVATE failsafe_headers Threads::Threads)
add_test(NAME hb_stress COMMAND hb_stress)
set_tests_properties(hb_stress PROPERTIES
  PASS_REGULAR_EXPRESSION "hb_stress OK"
  TIMEOUT 60)
