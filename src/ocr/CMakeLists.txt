add_library(lens_ocr STATIC
  http_transport.h
  online_ocr.h
  online_ocr.cpp
  online_providers.cpp
  ocr_baidu.h
  ocr_baidu.cpp
  ocr_space.h
  ocr_space.cpp
  qt_http_transport.h
  qt_http_transport.cpp
)

# src/ is exposed for self-contained "ocr/xxx.h" includes (same as lens_capture).
target_include_directories(lens_ocr PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt Network belongs to this target alone: lens_capture must not gain a path by which the scan
# pipeline could reach the network (docs/adr/0130).
target_link_libraries(lens_ocr
  PUBLIC lens_capture Qt6::Network
)
