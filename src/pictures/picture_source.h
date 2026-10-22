#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

#include <functional>

/**
 * @file picture_source.h
 * @brief The picture source contract: a query in, candidate pictures out (IMAGES.md 4.3).
 *
 * One source is wired at a time, and this interface exists so that swapping it -- another provider
 * with its own licence metadata -- is a new implementation rather than an edit to the duty. What a
 * source may be asked for is a query it searches with and a thumbnail it named itself; nothing about
 * the reader's page, selection or key ever reaches it.
 */

namespace lens::pictures {

/// @brief One picture a source found, with everything the card must show beside it.
/// @note `thumbnail` and `page` are the source's own addresses; `file` is filled in once the picture
///       has been cached, and is what the strip draws. A candidate without an author, a licence or a
///       source page is dropped while the answer is parsed rather than shown unattributed
///       (IMAGES.md 4.3).
struct Picture {
    QUrl    thumbnail; ///< The 480 px-wide thumbnail the source named, to download.
    QUrl    page;      ///< The source's page for the full image, which the browser opens.
    QString file;      ///< Local file the thumbnail is cached at; empty for a fresh candidate.
    QString caption;   ///< English caption: the source's description, or a cleaned file name.
    QString author;
    QString license; ///< Short licence name, such as "CC BY-SA 4.0".
    QString licenseUrl;
    QString source; ///< Provider display name, such as "Wikimedia Commons".
};

/// @brief Called once per search: the pictures found, and whether the source answered at all.
using PictureSink = std::function<void(QVector<Picture> const& pictures, bool ok)>;

/// @brief Called once per download: the bytes, and whether they arrived.
using ByteSink = std::function<void(QByteArray const& bytes, bool ok)>;

/**
 * @brief What one image source must answer.
 *
 * Both calls are asynchronous and answer exactly once. A refusal is @c ok false with no text: a
 * transport's own words never reach a surface, and the card shows one fallback line whatever went
 * wrong (IMAGES.md 4.5).
 */
class PictureSource {
public:
    virtual ~PictureSource() = default;

    /**
     * @brief Search for pictures of @p query.
     * @param query The `imageQuery` the model wrote -- never the reader's page or selection.
     * @param sink  Called with the usable pictures in the source's own order, or with @c ok false.
     *              An empty list with @c ok true is an answer: the source was reached and had
     *              nothing that passed its own filter.
     */
    virtual void search(QString const& query, PictureSink sink) = 0;

    /**
     * @brief Download one thumbnail @p url names.
     * @param url  An address this source named in a previous search.
     * @param sink Called with the image bytes, or with @c ok false for a refusal, a redirect this
     *             source will not follow, or a response over its ceiling.
     */
    virtual void fetch(QUrl const& url, ByteSink sink) = 0;
};

}