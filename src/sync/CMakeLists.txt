add_library(lens_sync STATIC
  sync_config.cpp
  github_auth.cpp
  gist_client.cpp
)

# src/ is exposed for self-contained "sync/xxx.h" includes, as lens_update does.
target_include_directories(lens_sync PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt is part of the public surface (QObject, QString, QUrl, the JSON types); lens_util is not.
target_link_libraries(lens_sync
  PUBLIC Qt6::Core Qt6::Network
  PRIVATE lens_util
)

# The User-Agent every sync call carries. The same number lens_update defines, from the root project().
target_compile_definitions(lens_sync PUBLIC LENS_VERSION="${PROJECT_VERSION}")
