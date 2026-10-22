/**
 * @file commons_source.cpp
 * @brief The Wikimedia Commons search and thumbnail download behind commons_source.h.
 */

#include "pictures/picture_sources/commons_source.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "pictures/picture_pure.h"
#include "util/log.h"

namespace lens::pictures {
namespace {

/// One search is a small JSON answer; a thumbnail is a picture. Both ceilings are generous for what
/// they carry and small enough that a hostile or broken answer cannot fill the disk (IMAGES.md 4.3).
constexpr qint64 kMaxSearchBytes = 2 * 1024 * 1024;
constexpr qint64 kMaxThumbBytes  = 2 * 1024 * 1024;
constexpr int    kTimeoutMs      = 4000;

/// The API asks for at most this many candidates; the filter keeps at most three of them.
constexpr int kSearchLimit = 12;

} // namespace

CommonsSource::CommonsSource(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1 (word image lookup; https://github.com/kakuyo1/lens)").arg(QString::fromUtf8(LENS_VERSION)))
{
}

QUrl CommonsSource::searchUrl(QString const& query)
{
    QUrl      url{QStringLiteral("https://commons.wikimedia.org/w/api.php")};
    QUrlQuery options;
    options.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    options.addQueryItem(QStringLiteral("generator"), QStringLiteral("search"));
    options.addQueryItem(QStringLiteral("gsrsearch"), query);
    options.addQueryItem(QStringLiteral("gsrnamespace"), QStringLiteral("6"));
    options.addQueryItem(QStringLiteral("gsrlimit"), QString::number(kSearchLimit));
    options.addQueryItem(QStringLiteral("prop"), QStringLiteral("imageinfo"));
    options.addQueryItem(QStringLiteral("iiprop"), QStringLiteral("url|size|mime|extmetadata"));
    options.addQueryItem(QStringLiteral("iiurlwidth"), QStringLiteral("480"));
    options.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    options.addQueryItem(QStringLiteral("formatversion"), QStringLiteral("2"));
    url.setQuery(options);
    return url;
}

void CommonsSource::search(QString const& query, PictureSink sink)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        sink({}, false);
        return;
    }

    QNetworkRequest request(searchUrl(trimmed));
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    // The query is the reader's word, which the existing lookup logs already carry; the address is
    // not logged whole (IMAGES.md 5).
    LENS_TRACE("picture search: commons, query '{}'", trimmed.toStdString());

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxSearchBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, sink] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError or status != 200) {
            // A status is an answer whatever it says, and the transport's own words never reach a
            // surface: the card says the same thing for every failure (IMAGES.md 4.5).
            LENS_WARN("the picture search did not answer: HTTP {} (error {})", status, static_cast<int>(reply->error()));
            sink({}, false);
            return;
        }
        const auto pictures = parseCommonsSearch(reply->readAll());
        LENS_TRACE("the picture search returned {} usable picture(s)", pictures.size());
        sink(pictures, true);
    });
}

void CommonsSource::fetch(QUrl const& url, ByteSink sink)
{
    // Only the upload hosts this source names in an answer: a redirect or a hand-edited cache entry
    // pointing anywhere else is refused here, so nothing this app downloads can be steered to
    // another machine (IMAGES.md 4.3).
    if (not isAllowedThumbnailHost(url)) {
        LENS_WARN("a thumbnail address was refused: it is not an upload host this source downloads from");
        sink({}, false);
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    // A redirect is not followed: the address checked above is the only one pixels may come from.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (received > kMaxThumbBytes or total > kMaxThumbBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, sink] {
        reply->deleteLater();
        const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes  = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
        if (reply->error() != QNetworkReply::NoError or status != 200 or bytes.isEmpty() or bytes.size() > kMaxThumbBytes) {
            sink({}, false);
            return;
        }
        sink(bytes, true);
    });
}

}