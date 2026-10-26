# gtest covers everything up to and including Qt Core: lens_core, and the LlmClient helpers
# that are Qt types but need no window. QML and widget tests arrive under test/qtest with
# QTest targets of their own.
#
# e2e/ has no cases yet, so it has no directory -- git does not track empty ones. Add the
# folder and an add_executable here when the first case lands.
#
# There is no ctest registration: the executables are run directly, as CLAUDE.md documents.
# gtest_discover_tests() would run each one during the build, which needs the Qt DLLs on
# PATH at build time and is not worth the coupling.

# Every target below reads the corpus, the word list and the protocol from the repository, so
# the source directory is baked in as an absolute path: the result must not depend on the cwd.
function(lens_gtest_configure target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${target} PRIVATE LENS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endfunction()

# No suite here is in the default build: `all` is the application alone, because the tests change
# far less often than src/ does and a developer rebuilding src/ should not pay for them. Every
# target carries EXCLUDE_FROM_ALL and is built by naming it -- scripts/build/build.bat passes a target
# through, so the command is `scripts/build/build.bat --target lens_gtest_unit`; CI names its own list,
# and TEST.md section 2 says when each suite is worth running.

# The offline suite: no network, no API key, safe for CI.
add_executable(lens_gtest_unit EXCLUDE_FROM_ALL
  unit/filter_core_test.cpp
  unit/stats_store_test.cpp
  unit/review_schedule_test.cpp
  unit/review_store_test.cpp
  unit/review_duty_test.cpp
  unit/sync_merge_test.cpp
  unit/sync_apply_test.cpp
  unit/sync_run_test.cpp
  unit/sync_round_test.cpp
  unit/sync_transport_test.cpp
  unit/sync_duty_test.cpp
  unit/settings_section_test.cpp
  unit/settings_defaults_test.cpp
  unit/settings_table_test.cpp
  unit/surface_table_test.cpp
  unit/words_view_test.cpp
  unit/app_seams_test.cpp
  unit/llm_pure_test.cpp
  unit/llm_mock_transport_test.cpp
  unit/llm_pricing_test.cpp
  unit/picture_pure_test.cpp
  unit/picture_duty_test.cpp
  unit/cost_duty_test.cpp
  unit/semver_test.cpp
  unit/update_pure_test.cpp
  unit/github_auth_test.cpp
  unit/explanation_cache_test.cpp
  unit/card_session_test.cpp
  unit/word_answer_test.cpp
  unit/surface_geometry_test.cpp
  unit/phase2_explanation_test.cpp
  unit/log_test.cpp
  unit/util_test.cpp
  unit/capture_test.cpp
  unit/pet_state_test.cpp
  unit/pet_mood_test.cpp
  unit/pet_hover_test.cpp
  unit/pet_config_test.cpp
  unit/pet_accessory_test.cpp
  unit/pet_data_test.cpp
  unit/pet_package_test.cpp
  unit/pet_catalog_test.cpp
  unit/pet_placement_test.cpp
  unit/pet_flight_test.cpp
  unit/pet_hide_test.cpp
  unit/pet_throw_test.cpp
  unit/pet_wander_test.cpp
  unit/pet_store_test.cpp
  unit/window_style_test.cpp
  unit/glass_math_test.cpp
  unit/glass_refresh_test.cpp
  unit/glass_desktop_test.cpp
  unit/glass_duplication_test.cpp
  unit/glass_source_test.cpp
  unit/glass_stats_test.cpp
  unit/dev_command_test.cpp
  unit/dev_line_framer_test.cpp
  unit/vault_test.cpp
)
target_link_libraries(lens_gtest_unit PRIVATE lens_app lens_core lens_llm lens_sync lens_update lens_util GTest::gtest_main)
lens_gtest_configure(lens_gtest_unit)

# Prints throughput and the profile breakdown; see the file header. Its only assertion is
# that the workload reached the pipeline, so this is a measurement, not a gate.
add_executable(lens_gtest_perf EXCLUDE_FROM_ALL
  perf/filter_perf_test.cpp
)
target_link_libraries(lens_gtest_perf PRIVATE lens_core GTest::gtest_main)
lens_gtest_configure(lens_gtest_perf)

# The glass backdrop's hardware ceiling: which adapter owns which output, whether that output
# duplicates, and what a frame costs against the GDI read used today. Measurement, not a gate,
# so it sits beside lens_gtest_perf. It links lens_app because it measures through the same
# desktop sources the application reads with (src/app/glass/glass_source.h), so it needs Qt DLLs
# on PATH like the other lens_app targets. Human-run: a CI runner has no desktop to duplicate.
add_executable(lens_gtest_glass_probe EXCLUDE_FROM_ALL
  perf/glass_capture_probe_test.cpp
)
target_link_libraries(lens_gtest_glass_probe PRIVATE lens_app user32 GTest::gtest_main)
lens_gtest_configure(lens_gtest_glass_probe)

# Needs a key and spends money, so it is never part of the default run: it reports a skip
# when settings.local.json has no credentials. Supplies its own main() to get an event loop,
# hence GTest::gtest rather than GTest::gtest_main.
add_executable(lens_gtest_smoke EXCLUDE_FROM_ALL
  smoke/llm_smoke_test.cpp
)
target_link_libraries(lens_gtest_smoke PRIVATE lens_core lens_llm GTest::gtest)
lens_gtest_configure(lens_gtest_smoke)

# The selection entry point: real Windows mouse and clipboard APIs, so it is not smoke (that
# name is the real-model round trip) and not unit (the interesting half needs a human to drag
# over another application). The gesture rule and the exclusion list are asserted offline
# here; the round trip reports a skip unless LENS_HOOK_SMOKE is set. Own main() for an event
# loop, hence GTest::gtest rather than gtest_main.
add_executable(lens_gtest_integration EXCLUDE_FROM_ALL
  integration/selection_capture_test.cpp
  integration/glass_read_pace_test.cpp
  integration/glass_duplication_test.cpp
  integration/glass_capture_test.cpp
)
target_link_libraries(lens_gtest_integration PRIVATE lens_app lens_core GTest::gtest)
lens_gtest_configure(lens_gtest_integration)
