#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

/**
 * @file pronunciation_source.h
 * @brief The recording source contract: one headword and one accent in, mp3 bytes out.
 *
 * One source is wired at a time. It exists so that the fallback ADR 0134 leaves open -- another
 * dictionary, or a service of the reader's own -- is a new implementation rather than an edit to
 * the duty, and so that a case can answer without a socket. What a source is asked for is a word
 * and an accent; nothing about the card, the sentence it came from or the reader reaches it.
 */

namespace lens::speech {

/// @brief Called once per request: the recording, and whether it arrived.
using AudioSink = std::function<void(QByteArray const& bytes, bool ok)>;

/**
 * @brief What one recording source must answer.
 *
 * The call is asynchronous and answers exactly once. A refusal is @c ok false with no text: a
 * transport's own words never reach a surface, and nothing in this feature reports a failure at all
 * (ADR 0107) -- the card simply stays quiet.
 */
class PronunciationSource {
public:
    virtual ~PronunciationSource() = default;

    /**
     * @brief Ask for one word's recording in one accent.
     * @param word   The card's headword, and nothing else. A caller that has a sentence has no
     *               business here: the endpoint answers 500 for one (API.md section 9).
     * @param accent "us" or "uk".
     * @param sink   Called with the mp3, or with @c ok false for a refusal, a timeout, a response
     *               over the source's ceiling, or anything that is not audio.
     */
    virtual void fetch(QString const& word, QString const& accent, AudioSink sink) = 0;
};

}
