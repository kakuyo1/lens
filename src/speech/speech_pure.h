#pragma once

#include <QString>

#include <optional>

/**
 * @file speech_pure.h
 * @brief The pronunciation rules that are values in and values out: which engine speaks, what the
 *        two switches store, and how a recording is named.
 *
 * Kept out of SpeechDuty for the reason picture_pure.h is kept out of PictureDuty: the rule that
 * decides whether anything is heard at all is the part worth a case, and a case should not need a
 * sound card, a socket or a settings document to reach it. No QObject and no event loop.
 *
 * The contract is docs/adr/0134: one word, a system synthesiser by default, and a fallback the
 * reader chose rather than one the code picks.
 */

namespace lens::speech {

/// @brief The engine the reader chose. The stored value is the lower-case name.
enum class Engine {
    System, ///< The Windows synthesiser Qt wraps. The default.
    Online, ///< The online recording the reader picked (API.md sections 9 and 9.2, docs/adr/0138).
    Local,  ///< sherpa-onnx with an English voice the reader downloaded.
};

/// @brief What is tried when the chosen engine cannot serve. Never tried after an Online or Local
///        engine failed on its own terms -- that would be the silent switch ADR 0134 forbids.
enum class Fallback {
    Online, ///< The default.
    Local,
    None, ///< The reader turned the fallback off.
};

/// @param value A stored `speechEngine`.
/// @return The engine it names, or System for anything else -- a hand-edited document reads as the
///         default rather than as silence.
Engine engineFrom(QString const& value);

/// @param value A stored `speechFallback`.
/// @return The fallback it names, or Online for anything else.
Fallback fallbackFrom(QString const& value);

/// @return @p engine as the settings document stores it.
QString engineName(Engine engine);

/// @return @p fallback as the settings document stores it.
QString fallbackName(Fallback fallback);

/// @return The accent a stored `speechAccent` names: "us" (the default) or "uk".
QString accentFrom(QString const& value);

/// @param accent "us" or "uk".
/// @return The `type` parameter the dictionary endpoint takes: 2 for American, 1 for British
///         (API.md section 9). Anything unrecognised answers American, which is the default accent.
QString onlineType(QString const& accent);

/**
 * @brief Which engine actually speaks, or nothing when the word stays silent.
 *
 * The rule, in the order ADR 0134 states it:
 *   - System chosen and usable: System.
 *   - System chosen and not usable: the fallback, which is Online, Local when its voice is
 *     downloaded, or nothing.
 *   - Online or Local chosen: that engine only. A reader who picked one has said which one, and
 *     slipping to the other on a failure would be the silent switch the ADR refuses. A local voice
 *     that is not downloaded therefore means silence, not a request to the dictionary.
 *
 * @param chosen        The reader's engine.
 * @param fallback      The reader's fallback.
 * @param systemUsable  Whether this machine has an engine and a voice for the accent.
 * @param localUsable   Whether the current accent's voice is downloaded and verified.
 * @return The engine to speak through, or std::nullopt for silence.
 */
std::optional<Engine> plan(Engine chosen, Fallback fallback, bool systemUsable, bool localUsable);

/**
 * @brief Whether @p word is something this feature may read.
 *
 * One word, as the card's headword is: not a sentence, not a translation, not an empty title. The
 * ceiling is generous for a headword and small enough that a card that somehow carried a paragraph
 * does not send it anywhere.
 *
 * @param word The card's title, already trimmed by the caller.
 * @return True for text of 1 to 64 characters with no line break in it.
 */
bool isHeadword(QString const& word);

/// @brief The online source the reader picked. The stored value is the lower-case id; each source is
///        one adapter (docs/adr/0138).
enum class OnlineSource {
    Youdao, ///< The dictionary's own recordings (API.md section 9). The default.
    Google, ///< Google Translate's speech endpoint, American or British by its language tag (API.md section 9.2).
};

/// @param value A stored `speechOnlineProvider`.
/// @return The source it names, or Youdao for anything else.
OnlineSource onlineSourceFrom(QString const& value);

/// @return @p source as the settings document stores it.
QString onlineSourceName(OnlineSource source);

/**
 * @brief The name a recording is cached under: `sha1(source|accent|word)`.
 *
 * The source is in it because two sources' recordings of one word are two different files, and a
 * reader who changes source must not be played the old one. The accent is in it for the same reason
 * between accents. The URL is deliberately not the name: a cache keyed by address would be keyed by a
 * constant endpoint plus a query string, and the accent would ride in it as a number that means nothing
 * to a reader looking at the folder.
 *
 * @param source The source the recording came from.
 * @param accent "us" or "uk".
 * @param word   The headword as the card shows it.
 * @return 40 lowercase hex digits, or empty when the digest could not be made.
 */
QString cacheKey(OnlineSource source, QString const& accent, QString const& word);

}
