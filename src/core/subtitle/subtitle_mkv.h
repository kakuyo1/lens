#pragma once

/**
 * @file subtitle_mkv.h
 * @brief The text subtitle tracks of a Matroska (.mkv/.webm) file, read by walking the EBML
 *        structure and stepping over every payload that is not a track header or a subtitle block.
 *
 * Only what a scan needs is decoded: the track entries (number, codec, language, flags) and the
 * cluster blocks of the text tracks. Video, audio and bitmap-subtitle payloads are skipped by
 * position, so the walk's cost is the element headers, not the media.
 */

#include "core/subtitle/subtitle.h"

#include <filesystem>
#include <vector>

namespace lens::core::subtitle {

/// @return Every text subtitle track @p path carries, cues filled and sorted by start. Bitmap
///         codecs and content-encoded tracks are skipped (the latter with a warning), as are laced
///         subtitle blocks; an unreadable file answers nothing.
std::vector<TextTrack> readMkvSubtitleTracks(std::filesystem::path const& path);

}
