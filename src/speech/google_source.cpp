/**
 * @file google_source.cpp
 * @brief The one GET behind google_source.h.
 */

#include "speech/google_source.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "util/log.h"

namespace lens::speech {

GoogleSource::GoogleSource(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1 (word pronunciation; https://github.com/kakuyo1/lens)").arg(QString::fromUtf8(LENS_VERSION)))
{
}

QUrl GoogleSource::urlFor(QString const& word, QString const& accent)
{
    QUrl      url{QStringLiteral("https://translate.google.com/translate_tts")};
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ie"), QStringLiteral("UTF-8"));
    query.addQueryItem(QStringLiteral("client"), QStringLiteral("tw-ob"));
    query.addQueryItem(QStringLiteral("tl"), accent == QLatin1String("uk") ? QStringLiteral("en-GB") : QStringLiteral("en-US"));
    // QUrlQuery encodes the word, so a word with an apostrophe in it is one query value.
    query.addQueryItem(QStringLiteral("q"), word);
    url.setQuery(query);
    return url;
}

void GoogleSource::fetch(QString const& word, QString const& accent, AudioSink sink)
{
    QNetworkRequest request(urlFor(word, accent));
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    // A redirect would carry the word to an address this source did not name, so it is not followed.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    LENS_TRACE("pronunciation: asking google for '{}' ({})", word.toStdString(), accent.toStdString());

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (received > kMaxBytes or total > kMaxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, sink] {
        reply->deleteLater();
        const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString    type   = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QByteArray bytes  = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
        // An answer that is not audio is a refusal, whatever its status says: the endpoint answers with HTML
        // for a request it does not like, and that must not reach the player.
        if (reply->error() != QNetworkReply::NoError or status != 200 or not type.startsWith(QLatin1String("audio/")) or bytes.isEmpty() or bytes.size() > kMaxBytes) {
            LENS_TRACE("pronunciation: no recording from google (HTTP {}, {} bytes, error {})", status, bytes.size(), static_cast<int>(reply->error()));
            sink({}, false);
            return;
        }
        sink(bytes, true);
    });
}

}
