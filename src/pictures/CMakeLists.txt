add_library(lens_pictures STATIC
  picture_pure.cpp
  picture_cache.cpp
  picture_sources/commons_source.cpp
)

# src/ is exposed for self-contained "pictures/xxx.h" includes (same as lens_llm and lens_core).
target_include_directories(lens_pictures PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt Gui is here for QImageReader: a cached thumbnail is only a picture once it decodes, and a
# downloaded response is only a picture for the same reason. lens_util is not part of the surface.
target_link_libraries(lens_pictures
  PUBLIC Qt6::Core Qt6::Gui Qt6::Network
  PRIVATE lens_util
)

# The version this build is, for the User-Agent the source sends. Same number as lens_llm's and
# lens_update's, and PUBLIC for the same reason: it comes from the root project(), not a literal.
target_compile_definitions(lens_pictures PUBLIC LENS_VERSION="${PROJECT_VERSION}")