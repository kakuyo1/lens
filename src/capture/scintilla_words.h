/** @file scintilla_words.h
 * @brief The word units of a Scintilla document, split from its UTF-8 bytes.
 *
 * The adapter reads a document once through SCI_GETTEXTRANGEFULL and walks these units; every
 * position it holds is a byte offset into that same buffer, so a word handle stays valid until the
 * document changes. Splitting is pure std and lives here so the scan's word shape can be tested
 * without a window.
 */
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace lens::capture {

/// @brief One word of a document, in byte offsets of the text it was split from.
struct ScintillaWordUnit {
    std::size_t start      = 0; ///< First letter or digit.
    std::size_t lettersEnd = 0; ///< Just past the last letter or digit; where a drawn line stops.
    std::size_t end        = 0; ///< Just past the punctuation that follows; the word unit's end.
};

/**
 * @brief Split @p text into word units, in reading order.
 *
 * A unit begins at a run of ASCII letters and digits (an apostrophe joins the run when both
 * neighbours are letters) and takes the ASCII punctuation that follows it. Whitespace, line breaks
 * and non-ASCII bytes separate units and are never part of one, so a unit's box stays on one line
 * and its whole text is valid UTF-8 when the document is.
 *
 * @param text UTF-8 document bytes.
 * @return The units; empty when the document holds none.
 */
std::vector<ScintillaWordUnit> scintillaWordUnits(std::string_view text);

}
