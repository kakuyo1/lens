# The crash reporter: the tiny pure-Win32 companion the dying process spawns (docs/adr/0175).
# No Qt, and deliberately not EXCLUDE_FROM_ALL -- it ships in the payload beside lens.exe and
# every default build carries it, which is what keeps the reporter one rebuild away from real.
enable_language(RC)
add_executable(lens_reporter WIN32
  reporter.cpp
  ${CMAKE_SOURCE_DIR}/data/icons/lens.rc
)

# The summary format and the issue URL come from lens_util, so the reporter cannot drift from
# the facade's copy; the log's spdlog stays out of the binary because nothing here references it.
target_link_libraries(lens_reporter PRIVATE lens_util user32 shell32)

# The app's UNICODE define comes down from the Qt targets it links; the reporter links none, and
# without the pair its IDI_APPLICATION and friends resolve to the ANSI macros.
target_compile_definitions(lens_reporter PRIVATE UNICODE _UNICODE)
