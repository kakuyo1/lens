# Remove the FFmpeg multimedia backend from a deployed payload.
#
# windeployqt deploys every multimedia plugin the Qt installation carries, and this Qt has two:
# windowsmediaplugin.dll and ffmpegmediaplugin.dll. Lens plays one word's 20 KB mp3 and nothing
# else, so the FFmpeg backend and the five libraries it drags in (17.5 MB) are weight the reader
# pays for and never uses. main.cpp pins QT_MEDIA_BACKEND=windows, so removing them is not a
# behaviour change: without the pin Qt would pick whichever backend it found first.
#
# Runs after windeployqt and before Inno Setup packs the payload, so the size the package report
# measures is the size the reader gets.
#
# Invoked as: cmake -DLENS_PAYLOAD_DIR=<payload> -P strip_media_plugins.cmake

if(NOT DEFINED LENS_PAYLOAD_DIR)
  message(FATAL_ERROR "strip_media_plugins.cmake needs LENS_PAYLOAD_DIR")
endif()

file(GLOB ffmpeg_plugins "${LENS_PAYLOAD_DIR}/multimedia/ffmpegmediaplugin*.dll")
file(GLOB ffmpeg_runtime
  "${LENS_PAYLOAD_DIR}/avcodec-*.dll"
  "${LENS_PAYLOAD_DIR}/avdevice-*.dll"
  "${LENS_PAYLOAD_DIR}/avfilter-*.dll"
  "${LENS_PAYLOAD_DIR}/avformat-*.dll"
  "${LENS_PAYLOAD_DIR}/avutil-*.dll"
  "${LENS_PAYLOAD_DIR}/swresample-*.dll"
  "${LENS_PAYLOAD_DIR}/swscale-*.dll"
)

foreach(file IN LISTS ffmpeg_plugins ffmpeg_runtime)
  file(REMOVE "${file}")
  message(STATUS "packaging: removed ${file}")
endforeach()

# The Windows backend has to be there: a package that shipped neither would play nothing, and the
# failure would show up as silence rather than as a missing file.
if(NOT EXISTS "${LENS_PAYLOAD_DIR}/multimedia/windowsmediaplugin.dll")
  message(FATAL_ERROR "windowsmediaplugin.dll is not in the payload; the package would play no audio")
endif()
