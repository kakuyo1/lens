/**
 * @file youdao_source.cpp
 * @brief The one GET behind youdao_source.h.
 */

#include "speech/youdao_source.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "speech/speech_pure.h"
#include "util/log.h"

namespace lens::speech {

YoudaoSource::YoudaoSource(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1 (word pronunciation; https://github.com/kakuyo1/lens)").arg(QString::fromUtf8(LENS_VERSION)))
{
}

QUrl YoudaoSource::urlFor(QString const& word, QString const& accent)
{
    QUrl      url{QStringLiteral("https://dict.youdao.com/dictvoice")};
    QUrlQuery query;
    // The word and the accent, and nothing else: no reader id, no install id, no page. QUrlQuery
    // encodes the word, so a word with an apostrophe or an accent in it is one query value.
    query.addQueryItem(QStringLiteral("audio"), word);
    query.addQueryItem(QStringLiteral("type"), onlineType(accent));
    url.setQuery(query);
    return url;
}

void YoudaoSource::fetch(QString const& word, QString const& accent, AudioSink sink)
{
    QNetworkRequest request(urlFor(word, accent));
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    // The endpoint answers where it is; a redirect would carry the word to an address this source
    // did not name, so it is not followed.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    LENS_TRACE("pronunciation: asking the dictionary for '{}' ({})", word.toStdString(), accent.toStdString());

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (received > kMaxBytes or total > kMaxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, sink] {
        reply->deleteLater();
        const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes  = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
        // A sentence, and some phrases, answer 500 with a 120-byte body: this is a dictionary entry's
        // recording, not a synthesiser, and a refusal is answered the way every other refusal here is.
        if (reply->error() != QNetworkReply::NoError or status != 200 or bytes.isEmpty() or bytes.size() > kMaxBytes) {
            LENS_TRACE("pronunciation: no recording (HTTP {}, {} bytes, error {})", status, bytes.size(), static_cast<int>(reply->error()));
            sink({}, false);
            return;
        }
        sink(bytes, true);
    });
}

}
