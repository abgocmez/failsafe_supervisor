# M0: a plain CTest test proving the CI pipeline end to end.
add_test(NAME sanity_runs COMMAND sanity)
set_tests_properties(sanity_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "sanity OK")

# M1 step 1: in-thread seqlock stress test (portable; runs in CI).
add_executable(hb_stress hb_stress.cpp)
target_link_libraries(hb_stress PRIVATE failsafe_headers Threads::Threads)
add_test(NAME hb_stress COMMAND hb_stress)
set_tests_properties(hb_stress PROPERTIES
  PASS_REGULAR_EXPRESSION "hb_stress OK" TIMEOUT 60)

# M1 step 2: cross-process seqlock over shared memory (fork; runs in CI).
add_executable(shm_fork_stress shm_fork_stress.cpp)
target_link_libraries(shm_fork_stress PRIVATE failsafe_ipc)
add_test(NAME shm_fork_stress COMMAND shm_fork_stress)
set_tests_properties(shm_fork_stress PROPERTIES
  PASS_REGULAR_EXPRESSION "shm_fork_stress OK" TIMEOUT 60)

# M1 integration: end-to-end deadline detection (spawns supervisor + worker).
add_test(NAME m1_smoke
  COMMAND ${CMAKE_COMMAND} -E env bash
          ${CMAKE_CURRENT_SOURCE_DIR}/m1_smoke.sh $<TARGET_FILE:supervisor>)
set_tests_properties(m1_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION "deadline_detect OK" TIMEOUT 30)

# M2 integration: restart recovery + budget escalation (spawns supervisor).
add_test(NAME m2_smoke
  COMMAND ${CMAKE_COMMAND} -E env bash
          ${CMAKE_CURRENT_SOURCE_DIR}/m2_smoke.sh $<TARGET_FILE:supervisor>)
set_tests_properties(m2_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION "m2_smoke OK" TIMEOUT 40)
