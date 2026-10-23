/**
 * @file picture_duty.cpp
 * @brief The picture lookup behind picture_duty.h.
 */

#include "picture_duty.h"

#include <QDesktopServices>
#include <QUrl>

#include "pictures/picture_pure.h"
#include "util/log.h"

namespace lens::app {
namespace {

/// How many thumbnails one query keeps, and the width the cache key names.
constexpr int kMaxPictures = 3;
/// The whole lookup's ceiling. A strip that has drawn nothing by then offers the fallback instead
/// (IMAGES.md 2.2); one that has drawn something keeps what it has.
constexpr int kDeadlineMs = 4000;

/// @return The strip's rows for a set of stored pictures.
QVariantList itemsOf(QVector<pictures::Picture> const& pictures)
{
    QVariantList items;
    for (auto const& picture : pictures) {
        items.append(QVariantMap{
            {QStringLiteral("image"), QUrl::fromLocalFile(picture.file).toString()},
            {QStringLiteral("caption"), picture.caption},
            {QStringLiteral("author"), picture.author},
            {QStringLiteral("license"), picture.license},
            {QStringLiteral("licenseUrl"), picture.licenseUrl},
            {QStringLiteral("page"), picture.page.toString()},
            {QStringLiteral("source"), picture.source},
        });
    }
    return items;
}

} // namespace

struct PictureDuty::Fetch {
    int                        id = 0;
    QString                    query;
    QString                    key;        ///< The normalized query the cache is keyed by.
    QVector<pictures::Picture> candidates; ///< What the source answered with.
    QVector<pictures::Picture> ready;      ///< The ones whose bytes arrived and decoded.
    QVector<QByteArray>        bytes;      ///< Their bytes, one per entry above.
    bool                       finished = false;
};

PictureDuty::PictureDuty(pictures::PictureSource& source, pictures::PictureCache& cache, QObject* parent)
    : QObject(parent), source_(source), cache_(cache)
{
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (live_ != nullptr)
            settle(live_);
    });
}

QVariantMap PictureDuty::pictures() const
{
    return QVariantMap{{QStringLiteral("state"), state_},
                       {QStringLiteral("query"), query_},
                       {QStringLiteral("items"), items_},
                       {QStringLiteral("fallback"), query_.isEmpty() ? QString{} : pictures::imageSearchUrl(query_)}};
}

void PictureDuty::request(QString query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        clear();
        return;
    }
    // Loading or already up is not asked for again: pressing the entry a second time is the reader
    // looking at the strip, not asking for another fetch. One that failed is, which is what makes
    // closing and reopening the entry the reader's retry (IMAGES.md 2.2).
    if (trimmed == query_ and (state_ == QStringLiteral("loading") or state_ == QStringLiteral("ready")))
        return;

    auto fetch   = std::make_shared<Fetch>();
    fetch->query = trimmed;
    fetch->key   = pictures::normalizedQuery(trimmed);

    // The cache is asked before anything is drawn: a picture already on disk is put up at once and
    // the network is not touched, so there is no skeleton and no second request (IMAGES.md 2.2).
    if (auto cached = cache_.load(fetch->key); cached and not cached->isEmpty()) {
        ++requestId_;
        live_ = nullptr;
        deadline_.stop();
        query_ = trimmed;
        items_ = itemsOf(*cached);
        state_ = QStringLiteral("ready");
        emit picturesChanged();
        return;
    }

    fetch->id = ++requestId_;
    live_     = fetch;
    query_    = trimmed;
    items_.clear();
    state_ = QStringLiteral("loading");
    emit picturesChanged();
    deadline_.start(kDeadlineMs);
    startSearch(fetch);
}

void PictureDuty::clear()
{
    // The number moves, so whatever is arriving is no longer the reader's: its answers land in the
    // cache and nowhere else.
    ++requestId_;
    live_ = nullptr;
    deadline_.stop();
    if (state_ == QStringLiteral("idle") and items_.isEmpty() and query_.isEmpty())
        return;
    query_.clear();
    items_.clear();
    state_ = QStringLiteral("idle");
    emit picturesChanged();
}

qint64 PictureDuty::cacheBytes() const
{
    return cache_.bytesOnDisk();
}

void PictureDuty::clearCache()
{
    cache_.clear();
    emit cacheBytesChanged();
}

void PictureDuty::openPage(QString url)
{
    const QUrl target{url};
    // Only https: the page is a link the browser opens, and a malformed answer must not turn the
    // strip into an arbitrary launch. The same rule UpdateDuty::openReleasePage follows.
    if (not target.isValid() or target.scheme() != QLatin1String("https") or target.host().isEmpty()) {
        LENS_WARN("a picture page was not opened: it is not an https address with a host");
        return;
    }
    QDesktopServices::openUrl(target);
}

void PictureDuty::openFallback()
{
    if (query_.isEmpty())
        return;
    // The query itself is already the class of data the lookup logs carry; the address is not logged
    // whole (IMAGES.md 5).
    LENS_TRACE("picture fallback opened for the query up");
    QDesktopServices::openUrl(QUrl{pictures::imageSearchUrl(query_)});
}

void PictureDuty::startSearch(const std::shared_ptr<Fetch>& fetch)
{
    source_.search(fetch->query, [this, fetch](QVector<pictures::Picture> const& pictures, bool ok) {
        if (fetch->finished)
            return;
        // Reached and empty, refused, or never reached: all three are the one fallback line the card
        // shows, and none of them is retried on its own (IMAGES.md 4.5).
        if (not ok or pictures.isEmpty()) {
            settle(fetch);
            return;
        }
        fetch->candidates = pictures;
        nextThumbnail(fetch, 0);
    });
}

void PictureDuty::nextThumbnail(const std::shared_ptr<Fetch>& fetch, int index)
{
    if (fetch->finished)
        return;
    if (index >= fetch->candidates.size() or fetch->ready.size() >= kMaxPictures) {
        settle(fetch);
        return;
    }
    source_.fetch(fetch->candidates.at(index).thumbnail, [this, fetch, index](QByteArray const& bytes, bool ok) {
        if (fetch->finished)
            return;
        // One picture that did not arrive or did not decode is dropped and the next is asked for:
        // the strip draws what it has rather than nothing (IMAGES.md 2.2). Whether the bytes are
        // really a picture is settled where they are stored.
        if (ok and not bytes.isEmpty()) {
            fetch->ready.push_back(fetch->candidates.at(index));
            fetch->bytes.push_back(bytes);
        }
        nextThumbnail(fetch, index + 1);
    });
}

void PictureDuty::settle(const std::shared_ptr<Fetch>& fetch)
{
    // One settle per lookup: the deadline and the last download can both arrive at it.
    if (fetch->finished)
        return;
    fetch->finished = true;

    // Whatever arrived goes to the cache first, so a strip the reader took down has still left its
    // pictures for the next open. What the card draws is exactly what the cache now holds.
    const auto stored = cache_.store(fetch->key, fetch->ready, fetch->bytes);
    if (not stored.isEmpty())
        emit cacheBytesChanged();
    if (fetch->id != requestId_)
        return; // a newer request owns the strip; this one only fed the cache

    deadline_.stop();
    live_ = nullptr;
    if (stored.isEmpty()) {
        // Nothing usable: not an empty strip with a placeholder in it, but the one line that offers
        // the reader the image search instead (IMAGES.md 4.5).
        items_.clear();
        state_ = QStringLiteral("failed");
        emit picturesChanged();
        return;
    }
    items_ = itemsOf(stored);
    state_ = QStringLiteral("ready");
    emit picturesChanged();
}

}