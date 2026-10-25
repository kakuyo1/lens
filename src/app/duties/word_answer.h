#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/document/known_store.h"
#include "llm/llm_client.h"

/**
 * @file word_answer.h
 * @brief One word answer, converted between the wire record, the stored entry and the card.
 *
 * The field list is util/word_fields.h; this module applies it to the three shapes the reader
 * keeps: the `llm::Explanation` a reply carries, the `core::WordCache` the store persists, and the
 * `QVariantMap` the bubble draws. ExplanationDuty used to spell these conversions itself, so a
 * new field was copied into four more places and the card's payload contract was only checked by
 * the tests that happened to build their own maps.
 *
 * The display switches are the reader's, read as the card is drawn: they decide which blocks a
 * payload carries and never which fields were bought or stored. The origin and the example are
 * bought settings, so off means the key is absent rather than empty.
 */

namespace lens::app::word_answer {

/// @brief The card's display switches, as the settings stand when it is drawn.
struct Switches {
    bool ipa          = true;
    bool pos          = true;
    bool signpost     = true;
    bool collocations = true;
    bool pictures     = true;
    bool etymology    = false;
    bool example      = false;
};

/// @return The line the card draws for this answer in @p language: en, zh, or the translation.
QString answerText(llm::Explanation const& answer, QString const& language);

/// @brief The answer blocks the card draws, with the switches applied.
/// @param level The reader's difficulty, which picks the definition links drawn in `spans`.
/// @return `ipa`, `pos`, `signpost`, `collocations`, `spans`, and `etymology` / `example` only
///         when their setting is on. The sense rows are the caller's: the card's cycle or its
///         multiple-sense selector decides which ones stand.
QVariantMap cardFields(llm::Explanation const& answer, QString const& language, Switches const& switches, int level);

/// @brief One sense as the card's selector row draws it: the language's text and its links.
QVariantMap senseRow(llm::Sense const& sense, QString const& language, bool pictures, int level);

/// @brief One step of the definition chain, as the panel under the card draws it.
QVariantMap chainEntry(llm::Explanation const& answer, QString const& language, Switches const& switches, int level);

/// @brief The words of an English definition the chain offers, as the UTF-16 spans ChainText draws.
QVariantList spansOf(QString const& text, int level);

/// @brief Put a paid-for answer into the stored shape.
/// @param exampleAsked Whether the batch asked for an example (docs/adr/0033): an answer bought
///                     without one stores no example at all, not an empty one.
core::WordCache toCache(llm::Explanation const& answer, bool exampleAsked);

/// @brief Read a stored entry as the record the card draws.
llm::Explanation fromCache(core::WordCache const& cached, QString const& lemma);

}
