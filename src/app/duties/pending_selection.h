#pragma once

#include <QPoint>
#include <QSize>
#include <QString>

#include "core/domain/filter_core.h"

/**
 * @file pending_selection.h
 * @brief What the reader pointed at: the one value the capture duty hands the explanation side.
 *
 * A value module's header: no duty, no window, no I/O. The queue, the card session and the exchange
 * all read it, and none of them has to include the capture duty to name their input.
 */

namespace lens::app {

struct PendingSelection {
    QPoint anchor;
    /// @brief The anchor's own size, for a card placed against a whole object rather than a point.
    ///        The word list's review bubble is placed above the word's rectangle, not at its corner.
    QSize   anchorSize;
    QString text;
    /// What the selection is, carried as the classifier's own kind: a value everywhere in C++, and a
    /// string only where a QML payload needs one (kindName()).
    core::SelectionKind kind = core::SelectionKind::Word;
    QString             surface;
    QString             lemma;
    QString             preset = QStringLiteral("default");
    /// @brief Whether this request is a step of the definition chain. Its answer goes onto the chain under
    ///        the card, and never onto the card itself.
    bool chain = false;
    /// @brief For a chain step, the word the card stands on. A step answers only while that is still the word
    ///        showing, so a step asked from a card the reader has since moved on from is dropped.
    QString parent;
};

/// @return The kind's one spelling on the QML wire, as the selection-bar and bubble payloads carry it.
QString kindName(core::SelectionKind kind);

/// @return Whether a selection is a word: the one kind the explanation cache, the sense modes and
///         the word's origin are about.
inline bool isWordKind(core::SelectionKind const kind)
{
    return kind == core::SelectionKind::Word;
}

/// @brief The preset a sentence selection carries once the reader asks for its breakdown. It is part
///        of the card's identity, so a breakdown and a translation or explanation of one text stay apart.
inline QString const kBreakdownPreset{QStringLiteral("breakdown")};
/// @brief The presets a spelling and a grammar correction carry, for the same reason as the breakdown's.
inline QString const kSpellPreset{QStringLiteral("spell")};
inline QString const kGrammarPreset{QStringLiteral("grammar")};

}
