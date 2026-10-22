#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

#include "pictures/picture_source.h"

/**
 * @file picture_pure.h
 * @brief The network-free half of the picture lookup: query hygiene, the cache key, the filter of a
 *        Commons answer, and the fallback address.
 *
 * Everything the picture path decides without a socket lives here, so the filter that decides which
 * of a source's candidates may be shown is exercised by a recorded answer rather than by a live
 * search (IMAGES.md 7). The source itself is in picture_sources/.
 */

namespace lens::pictures {

/// @brief Reduce a query to the one form the cache is keyed by: trimmed, single-spaced, lowercase.
/// @note This changes the key, never the question: the source is searched with the model's own text.
QString normalizedQuery(QString const& query);

/// @brief The cache key of @p normalizedQuery at one thumbnail width.
/// @param normalizedQuery A query already through normalizedQuery().
/// @param width           The thumbnail width the entry holds, 480 for the one size shipped.
QString cacheKey(QString const& normalizedQuery, int width);

/// @brief The image-search page the fallback button opens.
///
/// Fixed to an English page: the card's captions and its queries are English, so a search in the
/// reader's locale would answer with captions the card cannot read (IMAGES.md 4.5). The browser
/// decides whether the page is reachable; nothing is fetched here.
QString imageSearchUrl(QString const& query);

/// @brief Whether a thumbnail address is one this app will download from.
/// @param url An address a source named.
/// @return True for https on the upload hosts Commons serves thumbnails from. Anything else -- a
///         redirect target, an http address, a third-party host -- is refused (IMAGES.md 4.3).
bool isAllowedThumbnailHost(QUrl const& url);

/// @brief Whether @p bytes decode as an image this app can draw.
///
/// A response that arrives whole can still be an error page, an SVG or a truncated file: decoding is
/// what settles it, and one picture that fails to decode is dropped rather than shown broken.
bool decodesImage(QByteArray const& bytes);

/// @brief Strip the markup a Commons metadata field may carry, and collapse its whitespace.
QString plainText(QString const& value);

/**
 * @brief Turn a Commons search answer into the pictures the card may show.
 *
 * The answer is untrusted input filtered against everything IMAGES.md 4.3 lists: a photograph on
 * `image/jpeg` or `image/webp` only, at least 640 x 480 and landscape, on a licence that permits
 * display with attribution, with a source page and a thumbnail, and with a title or description that
 * does not name a diagram, map, logo or chart. The source's own search order is kept and at most
 * three survive.
 *
 * @param body Raw JSON body of the `action=query&generator=search` answer.
 * @return The pictures that passed, in search order, at most three. A body that is not an answer
 *         yields none rather than a partial one.
 */
QVector<Picture> parseCommonsSearch(QByteArray const& body);

}