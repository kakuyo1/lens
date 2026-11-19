#pragma once

/**
 * @file subtitle.h
 * @brief The scan's subtitle model: cue text with its media timing, the text tracks a video
 *        carries, and the cursor that finds the cue on screen at a playback position.
 *
 * Everything here is UTF-8 and Qt-free; the format parsers live beside this header
 * (subtitle_parse.h, subtitle_mkv.h), the choice of which track to read is subtitle_source.h's.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lens::core::subtitle {

/// @brief Where a cue sits vertically on screen: dialogue at the bottom, a top-anchored sign.
enum class Anchor : std::uint8_t {
    Bottom,
    Top,
};

/// @brief One subtitle cue: its media time span and the text as the player shows it, one entry per
///        displayed line, tags already stripped and empty lines dropped.
struct Cue {
    std::int64_t             startMs = 0;
    std::int64_t             endMs   = 0;
    Anchor                   anchor  = Anchor::Bottom;
    std::vector<std::string> lines;
};

/// @brief One text track of a video: its metadata and its cues, sorted by start.
struct TextTrack {
    int              number = 0; ///< The Matroska track number; 0 for an external file.
    std::string      codec;      ///< "S_TEXT/UTF8", "S_TEXT/ASS", or a file extension.
    std::string      language;   ///< Lowercase language tag; empty when the file gives none.
    std::string      name;       ///< The track's title, as the file spells it.
    bool             forced          = false;
    bool             hearingImpaired = false;
    bool             isDefault       = false;
    std::vector<Cue> cues;
};

/// @brief What loading found for one video: the chosen track's cues and where they came from.
struct SubtitleLoad {
    std::string      path; ///< The file the cues were read from, UTF-8; empty when none was found.
    std::string      codec;
    std::vector<Cue> cues;
    std::string      note; ///< Why nothing was found or what was skipped, for the log.
};

/**
 * @brief The cue on screen at a media time, found by a cursor that follows sequential playback and
 *        falls back to a binary search on any jump (a seek, or the first call).
 *
 * @note The cue list must stay sorted by startMs and outlive the timeline; overlapping cues are
 *       not merged, and the earliest covering cue answers.
 */
class CueTimeline {
public:
    /// @brief Point the timeline at @p cues; the cursor restarts at the first cue.
    void reset(std::vector<Cue> const* cues);

    /// @return The cue whose half-open span [start, end) holds @p mediaMs; nothing between cues.
    [[nodiscard]] Cue const* at(std::int64_t mediaMs);

    [[nodiscard]] bool empty() const
    {
        return cues_ == nullptr or cues_->empty();
    }

private:
    std::vector<Cue> const* cues_   = nullptr;
    std::size_t             cursor_ = 0;
};

inline void CueTimeline::reset(std::vector<Cue> const* const cues)
{
    cues_   = cues;
    cursor_ = 0;
}

inline Cue const* CueTimeline::at(std::int64_t const mediaMs)
{
    if (cues_ == nullptr or cues_->empty()) return nullptr;
    // Forward: the usual case -- the cue ends and the next one starts a moment later. A long jump
    // ahead walks at most the cues between, which a seek makes rare.
    while (cursor_ < cues_->size() and (*cues_)[cursor_].endMs <= mediaMs)
        ++cursor_;
    if (cursor_ < cues_->size()) {
        Cue const& cue = (*cues_)[cursor_];
        if (cue.startMs <= mediaMs) return &cue;
    }
    // The cursor now stands past every cue that has ended, which only a backwards seek (or a gap
    // between cues) explains: binary-search the span holding the time and re-seat the cursor.
    std::size_t low  = 0;
    std::size_t high = cues_->size();
    while (low < high) {
        std::size_t const mid = (low + high) / 2;
        if ((*cues_)[mid].startMs <= mediaMs)
            low = mid + 1;
        else
            high = mid;
    }
    if (low == 0) return nullptr;
    Cue const& cue = (*cues_)[low - 1];
    if (mediaMs < cue.endMs) {
        cursor_ = low - 1;
        return &cue;
    }
    cursor_ = low;
    return nullptr;
}

}
