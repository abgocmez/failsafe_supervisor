# M0: agir bir test cercevesi yerine, CI hattini uctan uca kanitlayan
# sade bir CTest testi. Gercek is mantigi M1de gelince Catch2 eklenecek.
add_test(NAME sanity_runs COMMAND sanity)
set_tests_properties(sanity_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "sanity OK")
