add_library(lens_core STATIC
  domain/filter_core.cpp
  domain/review_schedule.cpp
  domain/sync_merge.cpp
  domain/sync_state.cpp
  domain/pet/pet_state.cpp
  domain/pet/pet_config.cpp
  domain/pet/hover_sense.cpp
  domain/pet/pet_package.cpp
  domain/pet/accessory.cpp
  document/settings_section.cpp
  document/known_store.cpp
  document/explanation_cache.cpp
  document/stats_store.cpp
  document/review_store.cpp
  document/pet_store.cpp
  crypto/vault.cpp
  dev/dev_command.cpp
  dev/dev_line_framer.cpp
)

# src/ is exposed for self-contained "core/domain/xxx.h" includes; third_party/ for nlohmann/json.hpp
target_include_directories(lens_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party
)

# lens_util is PUBLIC: it carries util/log.h, util/profile.h and spdlog, which every consumer of lens_core uses.
target_link_libraries(lens_core PUBLIC lens_util)

# vault.cpp calls the Windows CNG provider. PRIVATE: vault.h names no BCrypt type, so consumers do not need it.
target_link_libraries(lens_core PRIVATE bcrypt)
