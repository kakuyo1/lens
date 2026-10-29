#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "speech/pronunciation_source.h"

class QNetworkAccessManager;

/**
 * @file google_source.h
 * @brief Google Translate's speech endpoint as a recording source: one GET, one mp3, no key (API.md section 9.2).
 *
 * The endpoint is unofficial and unauthenticated; it is the one the translate page's own speaker button
 * calls, and its terms are not verified here (docs/adr/0138). The address is a compile-time constant, like
 * the dictionary's: the reader picks the source, not the address the word is sent to.
 */

namespace lens::speech {

class GoogleSource final : public QObject, public PronunciationSource {
    Q_OBJECT
public:
    /// @brief One recording's ceiling. A word is about 7 KB from this endpoint, so this is generous and small.
    static constexpr qint64 kMaxBytes = 1024 * 1024;
    /// @brief The whole request's ceiling, measured from the send.
    static constexpr int kTimeoutMs = 10000;

    /// @param manager The network manager, or nullptr to own one.
    /// @param parent  The Qt parent, for the manager this object owns.
    explicit GoogleSource(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /**
     * @brief The address one word's recording is at.
     * @param word   The headword, URL-encoded here and nothing else appended to it.
     * @param accent "us" or "uk". The language tag is what picks the voice: en-US and en-GB are different recordings.
     * @return The GET address.
     */
    static QUrl urlFor(QString const& word, QString const& accent);

    void fetch(QString const& word, QString const& accent, AudioSink sink) override;

private:
    QNetworkAccessManager* manager_;
    QString                userAgent_;
};

}
