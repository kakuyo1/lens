/**
 * @file audio_cache.cpp
 * @brief The files behind audio_cache.h.
 */

#include "speech/audio_cache.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <vector>

#include "util/log.h"

namespace lens::speech {
namespace {

/// A key is 40 hex digits; anything else did not come from cacheKey() and is not a file name.
bool isKey(QString const& key)
{
    if (key.size() != 40)
        return false;
    for (const QChar character : key)
        if (not character.isDigit() and (character < QLatin1Char('a') or character > QLatin1Char('f')))
            return false;
    return true;
}

} // namespace

std::filesystem::path AudioCache::defaultRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path{base.toStdWString()} / L"audio";
}

AudioCache::AudioCache(std::filesystem::path root)
    : root_(std::move(root))
{
}

std::filesystem::path AudioCache::fileFor(QString const& key) const
{
    if (root_.empty() or not isKey(key))
        return {};
    return root_ / (key.toStdWString() + L".mp3");
}

std::optional<QByteArray> AudioCache::load(QString const& key) const
{
    const std::filesystem::path file = fileFor(key);
    if (file.empty())
        return std::nullopt;

    QFile stored(QString::fromStdWString(file.wstring()));
    if (not stored.exists())
        return std::nullopt;
    if (not stored.open(QIODevice::ReadOnly)) {
        std::error_code error;
        std::filesystem::remove(file, error);
        return std::nullopt;
    }
    const QByteArray bytes = stored.readAll();
    stored.close();
    if (bytes.isEmpty()) {
        std::error_code error;
        std::filesystem::remove(file, error);
        return std::nullopt;
    }

    // Wanted now: the file's time is this cache's only clock, and a hit that did not move it would
    // be the first thing the next prune threw away.
    std::error_code error;
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), error);
    return bytes;
}

bool AudioCache::store(QString const& key, QByteArray const& bytes) const
{
    const std::filesystem::path file = fileFor(key);
    if (file.empty() or bytes.isEmpty())
        return false;

    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
        return false;

    QSaveFile writer(QString::fromStdWString(file.wstring()));
    if (not writer.open(QIODevice::WriteOnly))
        return false;
    if (writer.write(bytes) != bytes.size() or not writer.commit()) {
        LENS_WARN("the pronunciation cache could not write one recording");
        return false;
    }
    prune();
    return true;
}

void AudioCache::prune() const
{
    if (root_.empty())
        return;
    std::error_code error;
    if (not std::filesystem::is_directory(root_, error))
        return;

    struct Entry {
        std::filesystem::file_time_type wantedAt;
        qint64                          bytes = 0;
        std::filesystem::path           path;
    };
    std::vector<Entry> entries;
    qint64             total = 0;

    for (auto const& item : std::filesystem::directory_iterator(root_, error)) {
        if (error)
            break;
        if (not item.is_regular_file(error) or item.path().extension() != L".mp3")
            continue;
        Entry entry;
        entry.path     = item.path();
        entry.bytes    = static_cast<qint64>(item.file_size(error));
        entry.wantedAt = item.last_write_time(error);
        if (error)
            break;
        total += entry.bytes;
        entries.push_back(entry);
    }
    if (total <= kMaxBytes)
        return;

    // Oldest first: a word the reader has not pressed in months goes before one they pressed today.
    std::sort(entries.begin(), entries.end(), [](Entry const& left, Entry const& right) {
        return left.wantedAt < right.wantedAt;
    });
    for (auto const& entry : entries) {
        if (total <= kMaxBytes)
            break;
        std::error_code removed;
        if (std::filesystem::remove(entry.path, removed))
            total -= entry.bytes;
    }
}

qint64 AudioCache::bytesOnDisk() const
{
    if (root_.empty())
        return 0;
    std::error_code error;
    if (not std::filesystem::is_directory(root_, error))
        return 0;

    qint64 total = 0;
    for (auto const& item : std::filesystem::directory_iterator(root_, error)) {
        if (error)
            break;
        if (item.is_regular_file(error))
            total += static_cast<qint64>(item.file_size(error));
    }
    return total;
}

void AudioCache::clear() const
{
    if (root_.empty())
        return;
    std::error_code error;
    std::filesystem::remove_all(root_, error);
}

}
