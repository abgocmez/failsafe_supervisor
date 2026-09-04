# M0: instead of a heavy test framework, a plain CTest test that proves the
# CI pipeline end to end. A real framework (Catch2) is added in M1 once there
# is business logic to test.
add_test(NAME sanity_runs COMMAND sanity)
set_tests_properties(sanity_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "sanity OK")
