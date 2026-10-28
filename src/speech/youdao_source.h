#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "speech/pronunciation_source.h"

class QNetworkAccessManager;

/**
 * @file youdao_source.h
 * @brief The dictionary's own recordings: one GET, one mp3, no key (API.md section 9).
 *
 * The endpoint is a compile-time constant and not a setting: it is not a service the reader
 * configures, and a reader who pointed it elsewhere would be sending the words they look up to a
 * machine of their own choosing, which is a different feature with a different consent (SECURITY.md
 * owns the outbound list). The address the reader's word travels to is the same one the About page
 * and the settings page name.
 */

namespace lens::speech {

class YoudaoSource final : public QObject, public PronunciationSource {
    Q_OBJECT
public:
    /// @brief One recording's ceiling: a word is 10-45 KB, so this is generous and still small.
    static constexpr qint64 kMaxBytes = 1024 * 1024;
    /// @brief The whole request's ceiling, measured from the send: the endpoint answers in 126-175 ms.
    static constexpr int kTimeoutMs = 10000;

    /// @param manager The network manager, or nullptr to own one.
    /// @param parent  The Qt parent, for the manager this object owns.
    explicit YoudaoSource(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /**
     * @brief The address one word's recording is at.
     * @param word   The headword, URL-encoded here and nothing else appended to it.
     * @param accent "us" or "uk".
     * @return The GET address, with `type` set from the accent.
     */
    static QUrl urlFor(QString const& word, QString const& accent);

    void fetch(QString const& word, QString const& accent, AudioSink sink) override;

private:
    QNetworkAccessManager* manager_;
    QString                userAgent_;
};

}
