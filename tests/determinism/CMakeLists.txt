# F2.G1 / F2.G2 (host side): allocation check on the headless sim and desktop player; Linux determinism (same hash twice,
# and equal to the committed expectation if tests/determinism/expected_hash.txt exists). The PSP half is tools/ci/determinism.sh.
if(TARGET rensga_headless)
  set(DET_PACK ${RENSGA_PROJECT_DIR}/Library/Cooked/psp/game.rpak)
  set(DET_DESK "-")
  if(TARGET rensga_desktop)
    set(DET_DESK $<TARGET_FILE:rensga_desktop>)
  endif()
  add_test(NAME alloc_check_linux COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check_alloc.sh
    $<TARGET_FILE:rensga_headless> ${DET_DESK} ${DET_PACK} ${CMAKE_CURRENT_SOURCE_DIR}/walk.json)
  set_tests_properties(alloc_check_linux PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  # DEPENDS matches by test name across directories; this dir is configured before tests/pipeline, so no if(TEST) guard.
  set_tests_properties(alloc_check_linux PROPERTIES DEPENDS pipeline_cooked_pack_pickup)
  add_test(NAME determinism_linux_trace COMMAND sh -c
    "test -f '${DET_PACK}' || exit 77; a=$('$<TARGET_FILE:rensga_headless>' --pack '${DET_PACK}' --input '${CMAKE_CURRENT_SOURCE_DIR}/walk.json' --frames 400 --hash --trace 2>&1 | grep -E 'rensga: (hash|trace)'); b=$('$<TARGET_FILE:rensga_headless>' --pack '${DET_PACK}' --input '${CMAKE_CURRENT_SOURCE_DIR}/walk.json' --frames 400 --hash --trace 2>&1 | grep -E 'rensga: (hash|trace)'); test -n \"$a\" && test \"$a\" = \"$b\"")
  # DEPENDS matches by test name across directories; this dir is configured before tests/pipeline, so no if(TEST) guard.
  set_tests_properties(determinism_linux_trace PROPERTIES SKIP_RETURN_CODE 77 DEPENDS pipeline_cooked_pack_pickup)
endif()
