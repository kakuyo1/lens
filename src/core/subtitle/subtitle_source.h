#pragma once

/**
 * @file subtitle_source.h
 * @brief Chooses which subtitle source the scan reads for a video: an external file beside it
 *        first, the embedded text tracks otherwise, by a scoring rule that prefers the dialogue a
 *        learner watches -- English, not forced, not SDH, not the bilingual or Chinese track.
 */

#include "core/subtitle/subtitle.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lens::core::subtitle {

/// @return The index of the track to read, or -1 when none qualifies. Forced tracks never
///         qualify, and neither does a track whose sampled text is mostly CJK; the English
///         language tag and the default flag raise a track's rank, SDH names and the
///         hearing-impaired flag lower it, and the earliest track breaks ties.
int chooseTextTrack(std::vector<TextTrack> const& tracks);

/// @return The external subtitle file to read for @p videoPath, or an empty path. Candidates share
///         the video's stem -- `stem.en.srt`, `stem.srt`, ...; an en/eng suffix outranks the bare
///         stem, which outranks any other suffix, then srt before vtt before ass before ssa, then
///         the file name.
std::filesystem::path chooseExternalSubtitle(std::filesystem::path const& videoPath);

/// @return What the scan should read for @p videoPath: the chosen external file's cues, else the
///         chosen embedded track's; no cues and a note saying why when neither answers.
SubtitleLoad loadSubtitlesFor(std::filesystem::path const& videoPath);

}
