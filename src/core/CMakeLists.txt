add_library(lens_core STATIC
  profile.cpp
  filter_core.cpp
  known_store.cpp
  review_schedule.cpp
  review_store.cpp
  settings_section.cpp
  stats_store.cpp
  sync_merge.cpp
  vault.cpp
  pet/pet_state.cpp
  pet/pet_config.cpp
  pet/pet_package.cpp
  pet/accessory.cpp
)

# src/ is exposed for self-contained "core/xxx.h" includes; third_party/ for nlohmann/json.hpp
target_include_directories(lens_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party
)

# lens_util is PUBLIC: it carries util/log.h and spdlog, which every consumer of lens_core uses.
target_link_libraries(lens_core PUBLIC lens_util)

# vault.cpp calls the Windows CNG provider. PRIVATE: vault.h names no BCrypt type, so consumers do not need it.
target_link_libraries(lens_core PRIVATE bcrypt)

# Profiling (core/profile.h) is off unless the build asks for it. PUBLIC, not PRIVATE: the
# macro decides whether a call site emits a sample, so lens_core and its callers must agree
# on the value or the reads and the writes would disagree about which sites exist.
target_compile_definitions(lens_core PUBLIC
  $<$<BOOL:${LENS_ENABLE_PROFILE}>:LENS_PROFILE=1>
)
