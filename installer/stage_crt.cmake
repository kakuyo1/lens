# Stage the MSVC CRT runtime DLLs that a dynamic-CRT build loads from beside the executable.
# The compiler toolchain is the only source of truth: walk up from CMAKE_CXX_COMPILER to the VC
# root that owns Redist/MSVC, then take the newest Microsoft.VC*.CRT directory for x64. There is
# no PATH fallback, like stage_ocr.cmake: a payload missing the CRT refuses to start on a machine
# without the VC++ redist, and that failure has to happen here rather than on a reader's desktop
# (docs/adr/0207).

foreach(required_variable IN ITEMS LENS_CRT_COMPILER LENS_CRT_STAGE_DIR)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "CRT staging requires -D${required_variable}=<value>")
  endif()
endforeach()

if(NOT EXISTS "${LENS_CRT_COMPILER}")
  message(FATAL_ERROR "CRT staging compiler not found: '${LENS_CRT_COMPILER}'.")
endif()

# The redist tree sits under the VC root (VC/Redist/MSVC/<version>/x64), not under the versioned
# VC/Tools/MSVC/<version> directory, and the two version numbers differ. Walk up until an
# ancestor owns Redist/MSVC.
set(LENS_CRT_VC_ROOT "${LENS_CRT_COMPILER}")
while(TRUE)
  get_filename_component(LENS_CRT_VC_ROOT "${LENS_CRT_VC_ROOT}" DIRECTORY)
  if(EXISTS "${LENS_CRT_VC_ROOT}/Redist/MSVC")
    break()
  endif()
  if(LENS_CRT_VC_ROOT MATCHES "^[A-Za-z]:[/]?$")
    message(FATAL_ERROR
      "CRT staging found no Redist/MSVC above '${LENS_CRT_COMPILER}'. "
      "Install the VC++ redistributable payload through the Visual Studio installer.")
  endif()
endwhile()

file(GLOB LENS_CRT_DIRS "${LENS_CRT_VC_ROOT}/Redist/MSVC/*/x64/Microsoft.VC*.CRT")
list(SORT LENS_CRT_DIRS)
if(NOT LENS_CRT_DIRS)
  message(FATAL_ERROR
    "CRT staging found no Microsoft.VC*.CRT directory under '${LENS_CRT_VC_ROOT}/Redist/MSVC'.")
endif()
# A VS update can leave several redist versions behind; the lexicographically last one is the
# newest, which is the one closest to the toolchain that built the application.
list(GET LENS_CRT_DIRS -1 LENS_CRT_DIR)

file(GLOB LENS_CRT_DLLS LIST_DIRECTORIES false "${LENS_CRT_DIR}/*.dll")
list(SORT LENS_CRT_DLLS)
if(NOT LENS_CRT_DLLS)
  message(FATAL_ERROR "CRT staging source is incomplete: no DLLs under '${LENS_CRT_DIR}'.")
endif()
list(LENGTH LENS_CRT_DLLS LENS_CRT_DLL_COUNT)

foreach(dll IN LISTS LENS_CRT_DLLS)
  file(COPY "${dll}" DESTINATION "${LENS_CRT_STAGE_DIR}")
endforeach()

message(STATUS "Staged ${LENS_CRT_DLL_COUNT} CRT runtime DLLs from '${LENS_CRT_DIR}' into '${LENS_CRT_STAGE_DIR}'")
