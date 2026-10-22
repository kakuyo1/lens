/**
 * @file picture_cache.cpp
 * @brief The reader's picture cache behind picture_cache.h.
 */

#include "pictures/picture_cache.h"

#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <system_error>

#include "pictures/picture_pure.h"
#include "util/log.h"

namespace lens::pictures {
namespace {

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// @return A filesystem path as the text Qt reads, so a non-ASCII account name survives.
QString qtPath(std::filesystem::path const& path)
{
    return QString::fromStdWString(path.wstring());
}

std::filesystem::path dirFor(std::filesystem::path const& root, QString const& key)
{
    return root / key.toStdWString();
}

QJsonObject readJson(std::filesystem::path const& file)
{
    QFile in(qtPath(file));
    if (not in.open(QIODevice::ReadOnly))
        return {};
    const auto document = QJsonDocument::fromJson(in.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

/// @brief Write a document beside its target and rename it into place.
/// @return Whether the document is now the file. A write that stops half-way leaves the previous
///         document intact, which is what makes an entry either wholly there or not there at all.
bool writeJsonAtomic(std::filesystem::path const& file, QJsonObject const& value)
{
    const std::filesystem::path temporary = file.parent_path() / (file.filename().wstring() + L".tmp");
    {
        QFile out(qtPath(temporary));
        if (not out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        out.write(QJsonDocument(value).toJson(QJsonDocument::Compact));
        out.close();
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

/// @return The extension the bytes' own magic names, or empty for a format this does not store.
/// @note Sniffed rather than taken from a header: the address the bytes came from says nothing
///       about what they are, and a cached file name is built from this.
QString sniffExtension(QByteArray const& bytes)
{
    const auto byte = [&bytes](int index) {
        return static_cast<unsigned char>(bytes.at(index));
    };
    if (bytes.size() >= 3 and byte(0) == 0xFF and byte(1) == 0xD8 and byte(2) == 0xFF)
        return QStringLiteral("jpg");
    if (bytes.size() >= 12 and bytes.startsWith("RIFF") and bytes.mid(8, 4) == "WEBP")
        return QStringLiteral("webp");
    return {};
}

/// @return Whether the file parses as an image, which is the cheapest proof that the cached bytes
///         are still a picture rather than a truncated file or a page that was served instead.
bool readableImage(std::filesystem::path const& path)
{
    QImageReader reader(qtPath(path));
    return reader.canRead() and reader.size().isValid();
}

} // namespace

std::filesystem::path PictureCache::defaultRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path{base.toStdWString()} / L"pictures" / L"cache" / L"v1";
}

PictureCache::PictureCache(std::filesystem::path root)
    : root_(std::move(root))
{
}

std::optional<QVector<Picture>> PictureCache::load(QString const& normalizedQuery) const
{
    if (normalizedQuery.isEmpty() or root_.empty())
        return std::nullopt;

    const std::filesystem::path directory = dirFor(root_, cacheKey(normalizedQuery, kThumbWidth));
    const std::filesystem::path metaFile  = directory / L"meta.json";
    const QJsonObject           meta      = readJson(metaFile);
    // No entry, or one whose metadata never landed: either way there is nothing a reader can use.
    if (meta.isEmpty())
        return std::nullopt;

    const qint64    fetchedAt = static_cast<qint64>(meta.value(QStringLiteral("fetchedAt")).toDouble());
    std::error_code error;
    if (fetchedAt <= 0 or nowMs() - fetchedAt > kTtlMs) {
        // Aged out. It goes now rather than being asked for again on this open only: the next
        // request would write the same directory, and leaving a stale one behind is a file nobody
        // will ever read.
        std::filesystem::remove_all(directory, error);
        LENS_TRACE("picture cache: an entry had aged out and was dropped");
        return std::nullopt;
    }

    QVector<Picture> pictures;
    for (auto const& value : meta.value(QStringLiteral("images")).toArray()) {
        const auto    image = value.toObject();
        const QString name  = image.value(QStringLiteral("file")).toString();
        // A name is something this class wrote, so anything that could leave the entry's own
        // directory is not one of ours.
        if (name.isEmpty() or name.contains(QLatin1Char('/')) or name.contains(QLatin1Char('\\')) or name.contains(QStringLiteral("..")))
            continue;
        const std::filesystem::path file = directory / name.toStdWString();
        if (not readableImage(file))
            continue;
        Picture picture;
        picture.file       = qtPath(file);
        picture.thumbnail  = QUrl{image.value(QStringLiteral("thumburl")).toString()};
        picture.page       = QUrl{image.value(QStringLiteral("page")).toString()};
        picture.caption    = image.value(QStringLiteral("caption")).toString();
        picture.author     = image.value(QStringLiteral("author")).toString();
        picture.license    = image.value(QStringLiteral("license")).toString();
        picture.licenseUrl = image.value(QStringLiteral("licenseUrl")).toString();
        picture.source     = image.value(QStringLiteral("source")).toString();
        pictures.push_back(std::move(picture));
    }
    if (pictures.isEmpty()) {
        // Metadata that names nothing readable is not an entry, and does not stay.
        std::filesystem::remove_all(directory, error);
        return std::nullopt;
    }

    // Touch the entry: the ceiling is enforced on last use, so a picture being looked at again is
    // the last one to go (IMAGES.md 4.4).
    QJsonObject touched = meta;
    touched.insert(QStringLiteral("usedAt"), static_cast<double>(nowMs()));
    writeJsonAtomic(metaFile, touched);
    LENS_TRACE("picture cache: hit, {} picture(s)", pictures.size());
    return pictures;
}

QVector<Picture> PictureCache::store(QString const& normalizedQuery, QVector<Picture> const& pictures, QVector<QByteArray> const& bytes) const
{
    if (normalizedQuery.isEmpty() or pictures.isEmpty() or root_.empty())
        return {};

    const std::filesystem::path directory = dirFor(root_, cacheKey(normalizedQuery, kThumbWidth));
    std::error_code             error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        LENS_WARN("picture cache: the entry directory could not be created; nothing was stored");
        return {};
    }

    QVector<Picture> stored;
    QJsonArray       images;
    for (int index = 0; index < pictures.size(); ++index) {
        const QByteArray imageBytes = index < bytes.size() ? bytes.at(index) : QByteArray{};
        if (not decodesImage(imageBytes))
            continue;
        const QString extension = sniffExtension(imageBytes);
        if (extension.isEmpty())
            continue;

        // Indices are the file names, so a picture that could not be stored leaves a hole rather
        // than shifting the ones behind it.
        const QString               name = QStringLiteral("%1.%2").arg(index).arg(extension);
        const std::filesystem::path file = directory / name.toStdWString();
        QFile                       out(qtPath(file));
        if (not out.open(QIODevice::WriteOnly | QIODevice::Truncate) or out.write(imageBytes) != imageBytes.size()) {
            LENS_WARN("picture cache: an image could not be written; it was left out");
            continue;
        }
        out.close();

        const QSize size    = QImageReader{qtPath(file)}.size();
        Picture     picture = pictures.at(index);
        picture.file        = qtPath(file);
        stored.push_back(picture);

        images.append(QJsonObject{
            {QStringLiteral("file"), name},
            {QStringLiteral("thumburl"), picture.thumbnail.toString()},
            {QStringLiteral("page"), picture.page.toString()},
            {QStringLiteral("caption"), picture.caption},
            {QStringLiteral("author"), picture.author},
            {QStringLiteral("license"), picture.license},
            {QStringLiteral("licenseUrl"), picture.licenseUrl},
            {QStringLiteral("source"), picture.source},
            {QStringLiteral("width"), size.width()},
            {QStringLiteral("height"), size.height()},
        });
    }

    if (stored.isEmpty()) {
        // Nothing survived: an entry with no pictures is not an entry, and is not left behind.
        std::filesystem::remove_all(directory, error);
        return {};
    }

    const qint64      stamp = nowMs();
    const QJsonObject meta{{QStringLiteral("query"), normalizedQuery},
                           {QStringLiteral("provider"), QStringLiteral("commons")},
                           {QStringLiteral("width"), kThumbWidth},
                           {QStringLiteral("fetchedAt"), static_cast<double>(stamp)},
                           {QStringLiteral("usedAt"), static_cast<double>(stamp)},
                           {QStringLiteral("images"), images}};
    if (not writeJsonAtomic(directory / L"meta.json", meta)) {
        LENS_WARN("picture cache: the entry's metadata could not be written; the entry is not readable");
        std::filesystem::remove_all(directory, error);
        return {};
    }

    prune();
    LENS_TRACE("picture cache: stored {} picture(s)", stored.size());
    return stored;
}

void PictureCache::prune() const
{
    if (root_.empty())
        return;
    std::error_code error;
    if (not std::filesystem::is_directory(root_, error))
        return;

    struct Entry {
        qint64                usedAt = 0;
        qint64                bytes  = 0;
        std::filesystem::path path;
    };
    QVector<Entry> entries;
    qint64         total = 0;

    for (auto const& item : std::filesystem::directory_iterator(root_, error)) {
        if (error)
            break;
        if (not item.is_directory())
            continue;
        const std::filesystem::path metaFile  = item.path() / L"meta.json";
        const QJsonObject           meta      = readJson(metaFile);
        const qint64                fetchedAt = static_cast<qint64>(meta.value(QStringLiteral("fetchedAt")).toDouble());
        qint64                      usedAt    = static_cast<qint64>(meta.value(QStringLiteral("usedAt")).toDouble());
        if (usedAt <= 0)
            usedAt = fetchedAt;

        // An entry that never got its metadata, and one that has aged out, can answer nothing: both
        // go here rather than being carried until something else happens to read them.
        if (meta.isEmpty() or fetchedAt <= 0 or nowMs() - fetchedAt > kTtlMs) {
            std::filesystem::remove_all(item.path(), error);
            error.clear();
            continue;
        }

        qint64 bytes = 0;
        for (auto const& file : std::filesystem::directory_iterator(item.path(), error)) {
            if (error)
                break;
            if (file.is_regular_file())
                bytes += static_cast<qint64>(file.file_size(error));
        }
        error.clear();
        entries.push_back({usedAt, bytes, item.path()});
        total += bytes;
    }

    std::sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) { return a.usedAt < b.usedAt; });

    int count = static_cast<int>(entries.size());
    for (auto const& entry : entries) {
        if (total <= kMaxBytes and count <= kMaxEntries)
            break;
        std::filesystem::remove_all(entry.path, error);
        error.clear();
        total -= entry.bytes;
        --count;
    }
}

}