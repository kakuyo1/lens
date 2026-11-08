#pragma once

/**
 * @file verdict_view.h
 * @brief The verdict projection, answered once: which line a word draws, and the token QML reads
 *        for it (docs/adr/0166).
 *
 * The judgement's semantics live in ADR 0159 and are computed by core::stateOf (the band question)
 * and KnownStore (the verdict question). What this header owns is the projection: every surface
 * that draws a line or fills a pill reads a token minted here, and never spells "new"/"known"/
 * "mastered" of its own. Before this module, four modules each assembled the string -- the scan
 * payload, the pencil layer's Word, the words popup's rows, and the bubble's badge -- and a fifth
 * parsed it back in mark(). Header-only and pure: no clock, no file, no QObject.
 */

#include <QString>

#include <string>
#include <unordered_set>

#include "core/document/known_store.h"
#include "core/domain/filter_core.h"

namespace lens::app {

/// @brief The token QML reads for the store's verdict: "new", "known" or "mastered" (docs/adr/0159).
/// @note None reads as "new", the state every unmarked word already carries (docs/adr/0147).
inline QString verdictToken(core::WordVerdict const verdict)
{
    switch (verdict) {
        case core::WordVerdict::None: return QStringLiteral("new");
        case core::WordVerdict::Known: return QStringLiteral("known");
        case core::WordVerdict::Mastered: return QStringLiteral("mastered");
    }
    return QStringLiteral("new");
}

/// @brief The token for a scan candidate's judged state. A Mastered candidate is never placed, so
///        the payload the pencil layer reads carries "new" or "known" only -- the token exists for
///        the one shape both verdict spaces share.
inline QString verdictToken(core::CandidateState const state)
{
    switch (state) {
        case core::CandidateState::New: return verdictToken(core::WordVerdict::None);
        case core::CandidateState::Known: return verdictToken(core::WordVerdict::Known);
        case core::CandidateState::Mastered: return verdictToken(core::WordVerdict::Mastered);
    }
    return QStringLiteral("new");
}

/// @brief The store's answer for @p lemma, as the bubble badge and the review card read it:
///        the reader's mastered verdict, else the known one, else new.
inline QString verdictToken(core::KnownStore const& store, std::string const& lemma)
{
    return verdictToken(store.isMasteredVerdict(lemma) ? core::WordVerdict::Mastered
                        : store.isKnown(lemma)         ? core::WordVerdict::Known
                                                       : core::WordVerdict::None);
}

/// @brief The verdict two verdict sets give a lemma, the way the words popup and its filter read it
///        (docs/adr/0159): mastered first -- the stronger verdict -- known second, new otherwise.
inline core::WordVerdict verdictOf(std::string const&                     lemma,
                                   std::unordered_set<std::string> const& knownLemmas,
                                   std::unordered_set<std::string> const& masteredLemmas)
{
    if (masteredLemmas.count(lemma) != 0) return core::WordVerdict::Mastered;
    if (knownLemmas.count(lemma) != 0) return core::WordVerdict::Known;
    return core::WordVerdict::None;
}

/// @brief The verdict a QML press names, parsed once: "mastered" and "known" are the two real
///        votes, anything else is the explicit "back to new" that rides sync (docs/adr/0147).
inline core::WordVerdict verdictFromToken(QString const& token)
{
    if (token == QLatin1String("mastered")) return core::WordVerdict::Mastered;
    if (token == QLatin1String("known")) return core::WordVerdict::Known;
    return core::WordVerdict::None;
}

}
