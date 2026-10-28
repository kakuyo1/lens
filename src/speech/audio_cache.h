#pragma once

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <optional>

/**
 * @file audio_cache.h
 * @brief The reader's own recordings: one mp3 per accent and word, under the profile.
 *
 * `%APPDATA%\Lens\audio\<sha1(accent|word)>.mp3`, kept under a ceiling and evicted least-recently
 * used first. It is the reader's private data: it is not in `settings.json`, not synced, not
 * uploaded, and a hit costs no request at all -- which is what makes a second press on a word
 * instant (API.md section 9, PRODUCT.md's storage table).
 *
 * The file's own last-write time is the clock. A metadata document beside each file, the way the
 * picture cache keeps one, would buy a hit a second read for no answer this cache needs: one file
 * per word, nothing to describe about it beyond when it was last wanted.
 */

namespace lens::speech {

class AudioCache {
public:
    /// @brief The ceiling the cache is kept under, oldest wanted first.
    static constexpr qint64 kMaxBytes = 50LL * 1024 * 1024;

    /// @return The cache under the reader's profile: `%APPDATA%\Lens\audio`.
    static std::filesystem::path defaultRoot();

    /// @param root The cache's directory; nothing is created until the first store.
    explicit AudioCache(std::filesystem::path root);

    /**
     * @brief The recording stored under @p key, if there is one.
     *
     * A hit is marked as wanted on the way out, which is what keeps it out of the next prune. A file
     * that cannot be read is removed rather than left to be found again.
     *
     * @param key A key from speech_pure.h's cacheKey().
     * @return The mp3 bytes, or nothing when the cache has no entry for the key.
     */
    std::optional<QByteArray> load(QString const& key) const;

    /**
     * @brief Store one recording and bring the cache back under its ceiling.
     *
     * The file is written through QSaveFile, so a run that stops half-way leaves the previous
     * recording rather than a truncated one: a half-written mp3 would play as noise.
     *
     * @param key   A key from cacheKey().
     * @param bytes The recording. Empty bytes are not stored.
     * @return Whether the recording is on disk afterwards.
     */
    bool store(QString const& key, QByteArray const& bytes) const;

    /// @brief Drop what falls outside the ceiling, oldest wanted first.
    void prune() const;

    /// @return The bytes every file under the root occupies, or 0 when there is no cache yet.
    qint64 bytesOnDisk() const;

    /// @brief Remove every recording, leaving the root absent until the next store.
    void clear() const;

    /// @return The root directory, for the log and for tests.
    const std::filesystem::path& root() const
    {
        return root_;
    }

private:
    /// @return The file one key is stored in.
    std::filesystem::path fileFor(QString const& key) const;

    std::filesystem::path root_;
};

}
