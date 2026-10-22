#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <filesystem>
#include <optional>

#include "pictures/picture_source.h"

/**
 * @file picture_cache.h
 * @brief The reader's own picture cache: thumbnails and their attribution, under the profile.
 *
 * One directory per query, keyed by the query's hash, holding the images and one metadata document.
 * The metadata is written last, so an entry that was interrupted half-way reads as absent rather
 * than as a broken picture; the directory is the reader's private data and is never synced, uploaded
 * or logged (IMAGES.md 4.4).
 */

namespace lens::pictures {

class PictureCache {
public:
    /// @brief The ceiling the cache is kept under, oldest used first.
    static constexpr qint64 kMaxBytes   = 64LL * 1024 * 1024;
    static constexpr int    kMaxEntries = 1000;
    /// @brief How long an entry answers before the next open buys it again.
    static constexpr qint64 kTtlMs = 30LL * 24 * 60 * 60 * 1000;

    /// @brief The thumbnail width the entry holds, and the width its key names.
    static constexpr int kThumbWidth = 480;

    /// @return The cache under the reader's profile: `%APPDATA%\Lens\pictures\cache\v1`.
    static std::filesystem::path defaultRoot();

    /// @param root The cache's root directory; nothing is created until the first store.
    explicit PictureCache(std::filesystem::path root);

    /**
     * @brief The stored pictures for a query.
     * @param normalizedQuery A query already through normalizedQuery().
     * @return The pictures with their `file` filled in, or nothing when there is no entry, the
     *         entry has aged past the TTL, or none of its images is still readable. An entry that
     *         cannot be used is deleted rather than left to be found again.
     */
    std::optional<QVector<Picture>> load(QString const& normalizedQuery) const;

    /**
     * @brief Store one query's pictures.
     *
     * The images are written first and the metadata document last, so a run that stops half-way
     * leaves no entry at all. The ceilings are enforced afterwards, on the same pass.
     *
     * @param normalizedQuery A query already through normalizedQuery().
     * @param pictures        The candidates, in the order the card should draw them.
     * @param bytes           One image per picture, in the same order. A picture whose bytes are
     *                        empty or whose format is not one of the two this stores is skipped, and
     *                        so is every picture after it -- indices are the file names.
     * @return The pictures that were written, each with `file` set, so the caller draws exactly
     *         what the next open will find. Empty when nothing could be stored.
     */
    QVector<Picture> store(QString const& normalizedQuery, QVector<Picture> const& pictures, QVector<QByteArray> const& bytes) const;

    /// @brief Drop what falls outside the ceilings and what has aged past the TTL, oldest used first.
    void prune() const;

    /// @return The root directory, for the log and for tests.
    const std::filesystem::path& root() const
    {
        return root_;
    }

private:
    std::filesystem::path root_;
};

}