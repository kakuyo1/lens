#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "pictures/picture_cache.h"
#include "pictures/picture_source.h"

/**
 * @file picture_duty.h
 * @brief The picture lookup one sense's entry asks for: cache first, the source otherwise.
 *
 * The rules live in picture_pure.h, the source in src/pictures/, the cache in PictureCache; this is
 * the QObject the card reads. It owns one lookup at a time -- the strip shows one sense -- and it is
 * the only place that knows what the reader is looking at, so a lookup that arrives after the reader
 * moved on is still cached and simply not drawn (IMAGES.md 4.2).
 */

namespace lens::app {

class PictureDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.pictureDuty")
public:
    /**
     * @brief Bind the duty to the source that searches and the cache that remembers.
     *
     * Both are taken rather than built, the same seam CostDuty and UpdateDuty take: the cache root
     * and the network manager are the caller's, so the duty's own decisions can be reached without
     * a socket.
     *
     * @param source The image source, owned by the caller.
     * @param cache  The picture cache, owned by the caller.
     */
    PictureDuty(pictures::PictureSource& source, pictures::PictureCache& cache, QObject* parent = nullptr);

    /**
     * @brief What the picture strip reads.
     *
     * @c state is `idle`, `loading`, `ready` or `failed`; @c query is the sense's `imageQuery`, which
     * is how the row that asked for the strip tells its own answer from another's; @c items is one
     * entry per picture, each carrying `image` (a local file URL the strip draws), `caption`,
     * `author`, `license`, `licenseUrl`, `page` and `source`; @c fallback is the image-search address
     * the failure button opens. Nothing here is ever a transport's own words.
     */
    QVariantMap pictures() const;

    /**
     * @brief Start the lookup for one sense's query.
     *
     * The cache is asked first, and a hit is drawn at once with no skeleton and no request. The same
     * query already loading or already up is not asked for twice; one that failed is, which is what
     * makes closing and reopening the entry the reader's retry.
     *
     * @param query The sense's `imageQuery`. An empty one clears the strip.
     */
    Q_INVOKABLE void request(QString query);

    /// @brief Take the strip down and stop drawing whatever was arriving.
    Q_INVOKABLE void clear();

    /// @return The bytes the cache holds, for the About page's one storage line.
    qint64 cacheBytes() const;

    /// @brief Delete every cached picture; the next open downloads them again.
    Q_INVOKABLE void clearCache();

    /// @brief Open a picture's source page in the reader's browser.
    /// @param url The `page` a picture's entry carries. Anything that is not https is refused.
    Q_INVOKABLE void openPage(QString url);

    /// @brief Open the image-search fallback for the query now up.
    Q_INVOKABLE void openFallback();

    Q_PROPERTY(QVariantMap pictures READ pictures NOTIFY picturesChanged)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheBytesChanged)

signals:
    /// @brief The state, the query, the pictures or the fallback address changed.
    void picturesChanged();
    /// @brief The cache grew or was cleared; the About page's size line reads it.
    void cacheBytesChanged();

private:
    /// @brief One lookup, alive until its last answer arrives.
    ///
    /// Held by the callbacks rather than by this object, so a lookup the reader has moved on from
    /// still completes and still writes its pictures to the cache (IMAGES.md 4.2).
    struct Fetch;

    void startSearch(const std::shared_ptr<Fetch>& fetch);
    void nextThumbnail(const std::shared_ptr<Fetch>& fetch, int index);
    /// @brief Cache whatever arrived and, when it is still the reader's, draw it.
    void settle(const std::shared_ptr<Fetch>& fetch);

    pictures::PictureSource& source_;
    pictures::PictureCache&  cache_;
    /// The ceiling on one lookup: search, downloads and decode together (IMAGES.md 2.2).
    QTimer deadline_;
    /// Which lookup owns the strip. Every answer carries the number it was asked under, and one that
    /// is not the current number is drawn nowhere.
    int                    requestId_ = 0;
    QString                query_;
    QString                state_ = QStringLiteral("idle");
    QVariantList           items_;
    std::shared_ptr<Fetch> live_;
};

}