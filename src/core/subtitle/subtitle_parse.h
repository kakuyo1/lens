#pragma once

/**
 * @file subtitle_parse.h
 * @brief Parsers for the text subtitle formats the scan meets: SubRip (.srt), WebVTT (.vtt) and
 *        the SubStation text formats (.ass / .ssa).
 *
 * The parsers are tolerant by policy: a malformed block is skipped, never fatal, because a
 * subtitle file that mostly reads is still worth scanning. Positioning overrides are the one thing
 * deliberately dropped -- a cue the player draws somewhere else than the default band cannot be
 * lined by geometry, so it is skipped rather than drawn in the wrong place.
 */

#include "core/subtitle/subtitle.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lens::core::subtitle {

/// @brief The visible text of one SubStation event's Text field: override blocks stripped, \N
///        breaks turned into line breaks, drawing runs dropped, line whitespace trimmed.
struct VisibleCue {
    Anchor                   anchor = Anchor::Bottom;
    std::vector<std::string> lines;
};

/// @brief Interpret one SubStation event's Text field.
/// @return Nothing when the text carries a positioning override (\pos, \move, \org, \clip), a
///         middle vertical anchor, or no letters once the tags are gone.
std::optional<VisibleCue> assVisibleText(std::string_view text);

/// @brief Parse a SubRip document.
/// @return Its cues in file order; blocks without a readable timing line are skipped, and an end
///         not after its start is repaired to start + 1 s.
std::vector<Cue> parseSrt(std::string_view text);

/// @brief Parse an Advanced SubStation document.
/// @return The [Events] Dialogue lines as cues, tags stripped (see assVisibleText).
std::vector<Cue> parseAss(std::string_view text);

/// @brief Parse a WebVTT document.
/// @return Its cues in file order; cue identifiers and settings are ignored.
std::vector<Cue> parseVtt(std::string_view text);

/// @brief Read a subtitle file, dispatched by its extension (srt, vtt, ass, ssa).
/// @param codec Set to the lower-case extension when the file was read.
/// @return Its cues, sorted by start; empty when the extension is unsupported or the file cannot
///         be read.
std::vector<Cue> parseSubtitleFile(std::filesystem::path const& path, std::string* codec = nullptr);

}
