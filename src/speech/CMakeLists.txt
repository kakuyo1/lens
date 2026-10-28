add_library(lens_speech STATIC
  speech_pure.cpp
  audio_cache.cpp
  youdao_source.cpp
)

# src/ is exposed for self-contained "speech/xxx.h" includes (same as lens_pictures and lens_core).
target_include_directories(lens_speech PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt Network for the one GET; Core alone would not carry QNetworkAccessManager. lens_core is
# PRIVATE: the cache's key is a SHA-1 through core/crypto/hash.h, and nothing in a speech header
# names a core type.
target_link_libraries(lens_speech
  PUBLIC Qt6::Core Qt6::Network
  PRIVATE lens_core lens_util
)

# The version this build is, for the User-Agent the source sends. Same number as lens_pictures', and
# PUBLIC for the same reason: it comes from the root project(), not a literal.
target_compile_definitions(lens_speech PUBLIC LENS_VERSION="${PROJECT_VERSION}")
