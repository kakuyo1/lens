/**
 * @file picture_pure_test.cpp
 * @brief The picture lookup's network-free half: the query key, the Commons filter, the fallback
 *        address, the decode check, and the cache on a temp directory.
 *
 * The filter cases replay a recorded Commons answer rather than searching, so which candidate the
 * card may show is decided here and not by a paid round trip.
 */

#include <filesystem>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <gtest/gtest.h>

#include "pictures/picture_cache.h"
#include "pictures/picture_pure.h"

namespace {

using lens::pictures::Picture;
using lens::pictures::PictureCache;

/// @return One Commons page, its `imageinfo` holding a single entry.
QJsonObject page(QString const& title, QJsonObject const& info, int index = 1, int ns = 6)
{
    return QJsonObject{{"title", title}, {"ns", ns}, {"index", index}, {"imageinfo", QJsonArray{info}}};
}

/// @return A plausible Commons imageinfo entry, with @p overrides applied over it.
QJsonObject imageInfo(QJsonObject const& overrides = {})
{
    QJsonObject metadata{{"LicenseShortName", QJsonObject{{"value", "CC BY-SA 4.0"}}},
                         {"LicenseUrl", QJsonObject{{"value", "https://creativecommons.org/licenses/by-sa/4.0/"}}},
                         {"Artist", QJsonObject{{"value", "<a href=\"//commons.wikimedia.org/wiki/User:X\">A Person</a>"}}},
                         {"ImageDescription", QJsonObject{{"value", "A garden trowel on soil"}}}};
    QJsonObject info{{"mime", "image/jpeg"},
                     {"width", 2000},
                     {"height", 1400},
                     {"descriptionurl", "https://commons.wikimedia.org/wiki/File:Garden_trowel.jpg"},
                     {"thumburl", "https://upload.wikimedia.org/wikipedia/commons/thumb/a/ab/Garden_trowel.jpg/480px-Garden_trowel.jpg"},
                     {"extmetadata", metadata}};
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it.key() == QLatin1String("extmetadata"))
            info.insert(it.key(), it.value());
        else
            info.insert(it.key(), it.value());
    }
    return info;
}

/// @return An imageinfo entry with one metadata field replaced by @p value.
QJsonObject withMetadata(QString const& key, QJsonObject const& value)
{
    QJsonObject info     = imageInfo();
    QJsonObject metadata = info.value("extmetadata").toObject();
    metadata.insert(key, value);
    info.insert("extmetadata", metadata);
    return info;
}

QByteArray bodyOf(QJsonArray const& pages)
{
    return QJsonDocument(QJsonObject{{"batchcomplete", true}, {"query", QJsonObject{{"pages", pages}}}})
        .toJson(QJsonDocument::Compact);
}

/// @return Bytes the app must accept as a picture: a real JPEG, encoded here rather than shipped.
QByteArray jpegBytes()
{
    QImage image(8, 6, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer    buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    EXPECT_TRUE(image.save(&buffer, "JPG"));
    return bytes;
}

} // namespace

/// A photograph on a licence that permits display, at the size and shape the card needs, survives
/// with its attribution, and the HTML Commons writes around an artist is stripped.
TEST(ParseCommonsSearch, KeepsAPhotographAndItsAttribution)
{
    const auto pictures = lens::pictures::parseCommonsSearch(bodyOf(QJsonArray{page("File:Garden trowel.jpg", imageInfo())}));

    ASSERT_EQ(pictures.size(), 1);
    EXPECT_EQ(pictures.front().source, QStringLiteral("Wikimedia Commons"));
    EXPECT_EQ(pictures.front().license, QStringLiteral("CC BY-SA 4.0"));
    EXPECT_EQ(pictures.front().author, QStringLiteral("A Person")) << "the anchor markup survived";
    EXPECT_EQ(pictures.front().caption, QStringLiteral("A garden trowel on soil"));
    EXPECT_TRUE(pictures.front().thumbnail.host() == QStringLiteral("upload.wikimedia.org"));
    EXPECT_TRUE(pictures.front().page.host() == QStringLiteral("commons.wikimedia.org"));
}

/// Everything the filter refuses, refused separately so a regression names the rule it broke.
TEST(ParseCommonsSearch, DropsWhateverTheCardMayNotShow)
{
    const auto only = [](QJsonArray const& pages) {
        return lens::pictures::parseCommonsSearch(bodyOf(pages)).size();
    };

    EXPECT_EQ(only({page("File:X.svg", imageInfo(QJsonObject{{"mime", "image/svg+xml"}}))}), 0) << "an SVG is not a photograph";
    EXPECT_EQ(only({page("File:X.jpg", imageInfo(QJsonObject{{"width", 320}, {"height", 240}}))}), 0) << "too small";
    EXPECT_EQ(only({page("File:X.jpg", imageInfo(QJsonObject{{"width", 800}, {"height", 1400}}))}), 0) << "portrait";
    EXPECT_EQ(only({page("File:X.jpg", imageInfo(QJsonObject{{"width", 1000}, {"height", 1000}}))}), 0) << "square";
    EXPECT_EQ(only({page("File:X.jpg", withMetadata("LicenseShortName", QJsonObject{{"value", "GFDL"}}))}), 0) << "licence";
    EXPECT_EQ(only({page("File:X.jpg", imageInfo(QJsonObject{{"descriptionurl", ""}}))}), 0) << "no source page";
    EXPECT_EQ(only({page("File:X.jpg", imageInfo(QJsonObject{{"thumburl", ""}}))}), 0) << "no thumbnail";
    EXPECT_EQ(only({page("File:Map of Europe.jpg", imageInfo())}), 0) << "a map is not a photograph";
    EXPECT_EQ(only({page("File:X.jpg", withMetadata("ImageDescription", QJsonObject{{"value", "A diagram of a trowel"}}))}), 0)
        << "a description that names a drawing";
    EXPECT_EQ(only({page("File:X.jpg", imageInfo(), 1, 0)}), 0) << "an article, not a file";
}

/// A picture whose author is missing keeps its place with a plain attribution, because the card has
/// to show something; one whose licence is unknown does not get that far.
TEST(ParseCommonsSearch, NamesAMissingAuthorRatherThanDroppingThePicture)
{
    auto info     = imageInfo();
    auto metadata = info.value("extmetadata").toObject();
    metadata.remove("Artist");
    info.insert("extmetadata", metadata);

    const auto pictures = lens::pictures::parseCommonsSearch(bodyOf(QJsonArray{page("File:Trowel.jpg", info)}));
    ASSERT_EQ(pictures.size(), 1);
    EXPECT_EQ(pictures.front().author, QStringLiteral("Unknown author"));
}

/// The caption is the source's own description when it is in English, and the file name otherwise:
/// the card never shows a caption it cannot read, and never machine-translates one.
TEST(ParseCommonsSearch, FallsBackToTheFileNameForACaptionItCannotRead)
{
    const auto nonEnglish = lens::pictures::parseCommonsSearch(bodyOf(QJsonArray{
        page("File:Garden_trowel-01.jpg", withMetadata("ImageDescription", QJsonObject{{"value", QString::fromUtf8("园艺铲")}}))}));
    ASSERT_EQ(nonEnglish.size(), 1);
    EXPECT_EQ(nonEnglish.front().caption, QStringLiteral("Garden trowel 01"));

    const auto missing = lens::pictures::parseCommonsSearch(bodyOf(QJsonArray{
        page("File:Garden_trowel.jpg", withMetadata("ImageDescription", QJsonObject{{"value", ""}}))}));
    ASSERT_EQ(missing.size(), 1);
    EXPECT_EQ(missing.front().caption, QStringLiteral("Garden trowel"));
}

/// The source's search order is the order the card draws, and at most three survive.
TEST(ParseCommonsSearch, KeepsSearchOrderAndAtMostThree)
{
    QJsonArray pages;
    for (int index = 5; index >= 1; --index) {
        pages.append(page(QStringLiteral("File:X%1.jpg").arg(index),
                          withMetadata("ImageDescription", QJsonObject{{"value", QStringLiteral("photo %1").arg(index)}}),
                          index));
    }

    const auto pictures = lens::pictures::parseCommonsSearch(bodyOf(pages));
    ASSERT_EQ(pictures.size(), 3);
    EXPECT_EQ(pictures.at(0).caption, QStringLiteral("photo 1"));
    EXPECT_EQ(pictures.at(1).caption, QStringLiteral("photo 2"));
    EXPECT_EQ(pictures.at(2).caption, QStringLiteral("photo 3"));
}

/// A body that is not an answer yields nothing rather than a partial one: the card's fallback line
/// is the one thing a reader sees for every kind of failure.
TEST(ParseCommonsSearch, AnswersNothingForSomethingThatIsNotAnAnswer)
{
    EXPECT_TRUE(lens::pictures::parseCommonsSearch(QByteArray{}).isEmpty());
    EXPECT_TRUE(lens::pictures::parseCommonsSearch("not json").isEmpty());
    EXPECT_TRUE(lens::pictures::parseCommonsSearch(R"({"query":{"pages":[]}})").isEmpty());
}

/// The cache key ignores case and spacing, which is what makes the same sense's query one entry, and
/// names the width so a future size is a different entry rather than a wrong picture.
TEST(PictureQuery, NormalizesAndKeysByQueryAndWidth)
{
    EXPECT_EQ(lens::pictures::normalizedQuery(QStringLiteral("  Garden   Trowel \n")), QStringLiteral("garden trowel"));
    const auto one = lens::pictures::cacheKey(QStringLiteral("garden trowel"), 480);
    EXPECT_EQ(one, lens::pictures::cacheKey(QStringLiteral("garden trowel"), 480));
    EXPECT_NE(one, lens::pictures::cacheKey(QStringLiteral("crane bird"), 480));
    EXPECT_NE(one, lens::pictures::cacheKey(QStringLiteral("garden trowel"), 240));
    EXPECT_EQ(one.size(), 64) << "the key is a sha256 in hex";
}

/// The fallback is an English image search on https, with the query encoded: an unencoded one would
/// be cut at its first space.
TEST(PictureQuery, BuildsAnEnglishImageSearchAddress)
{
    const QUrl url{lens::pictures::imageSearchUrl(QStringLiteral("garden trowel"))};
    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("www.bing.com"));
    // FullyEncoded, because query() pretty-decodes %20 back to a space for display.
    const QString query = url.query(QUrl::FullyEncoded);
    EXPECT_TRUE(query.contains(QStringLiteral("q=garden%20trowel"))) << query.toStdString();
    EXPECT_TRUE(query.contains(QStringLiteral("setlang=en"))) << query.toStdString();
}

/// Only the hosts this source serves thumbnails from, over https: a redirect target or a third-party
/// host is a download this app will not make.
TEST(PictureQuery, AllowsOnlyTheUploadHostsOverHttps)
{
    EXPECT_TRUE(lens::pictures::isAllowedThumbnailHost(QUrl{QStringLiteral("https://upload.wikimedia.org/x.jpg")}));
    EXPECT_TRUE(lens::pictures::isAllowedThumbnailHost(QUrl{QStringLiteral("https://thumb.wikimedia.org/x.jpg")}));
    EXPECT_FALSE(lens::pictures::isAllowedThumbnailHost(QUrl{QStringLiteral("http://upload.wikimedia.org/x.jpg")}));
    EXPECT_FALSE(lens::pictures::isAllowedThumbnailHost(QUrl{QStringLiteral("https://evil.example/x.jpg")}));
    EXPECT_FALSE(lens::pictures::isAllowedThumbnailHost(QUrl{QStringLiteral("https://commons.wikimedia.org/x.jpg")}));
    EXPECT_FALSE(lens::pictures::isAllowedThumbnailHost(QUrl{}));
}

/// Decoding is what tells a picture from an error page or a truncated file, so it is the one check
/// the download and the cache both stand on.
TEST(PictureQuery, DecodesOnlyWhatIsReallyAPicture)
{
    EXPECT_TRUE(lens::pictures::decodesImage(jpegBytes()));
    EXPECT_FALSE(lens::pictures::decodesImage(QByteArray{}));
    EXPECT_FALSE(lens::pictures::decodesImage(QByteArrayLiteral("<html>nope</html>")));
    EXPECT_FALSE(lens::pictures::decodesImage(jpegBytes().left(20))) << "a truncated JPEG has a valid header";
}

/// @brief The cache on a directory of its own, so nothing is written outside the run.
struct PictureCacheTest : ::testing::Test {
protected:
    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() / "lens_picture_cache_test";
        std::filesystem::remove_all(root);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }

    /// @return One candidate whose bytes are a real picture, as the source would hand it over.
    static Picture candidate(QString const& page_url = QStringLiteral("https://commons.wikimedia.org/wiki/File:X.jpg"))
    {
        Picture picture;
        picture.thumbnail = QUrl{QStringLiteral("https://upload.wikimedia.org/x.jpg")};
        picture.page      = QUrl{page_url};
        picture.caption   = QStringLiteral("a hand tool");
        picture.author    = QStringLiteral("A Person");
        picture.license   = QStringLiteral("CC BY-SA 4.0");
        picture.source    = QStringLiteral("Wikimedia Commons");
        return picture;
    }

    std::filesystem::path root;
};

/// A stored entry comes back with its files, and the second read never touches the source.
TEST_F(PictureCacheTest, StoresAndReadsBackOneEntry)
{
    const PictureCache cache{root};
    const auto         stored = cache.store(QStringLiteral("garden trowel"), {candidate()}, {jpegBytes()});
    ASSERT_EQ(stored.size(), 1);
    ASSERT_FALSE(stored.front().file.isEmpty());
    {
        QFile file(stored.front().file);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << "the stored picture is not on disk";
        EXPECT_TRUE(lens::pictures::decodesImage(file.readAll())) << "the stored bytes are not a picture";
    }

    const auto loaded = cache.load(QStringLiteral("garden trowel"));
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->size(), 1);
    EXPECT_EQ(loaded->front().file, stored.front().file);
    EXPECT_EQ(loaded->front().author, QStringLiteral("A Person"));
    EXPECT_EQ(loaded->front().caption, QStringLiteral("a hand tool"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path{loaded->front().file.toStdWString()}));
}

/// An entry no query wrote yet is a miss, and a query whose bytes were not a picture stores nothing
/// rather than an entry with no picture in it.
TEST_F(PictureCacheTest, MissingAndUnstorableAreBothMisses)
{
    const PictureCache cache{root};
    EXPECT_FALSE(cache.load(QStringLiteral("nothing here")).has_value());

    EXPECT_TRUE(cache.store(QStringLiteral("garden trowel"), {candidate()}, {QByteArrayLiteral("nope")}).empty());
    EXPECT_FALSE(cache.load(QStringLiteral("garden trowel")).has_value());
    EXPECT_FALSE(std::filesystem::exists(root / lens::pictures::cacheKey(QStringLiteral("garden trowel"), PictureCache::kThumbWidth).toStdWString()))
        << "an entry with nothing in it was left behind";
}

/// An entry that has aged past the TTL is a miss and is dropped, so the next open asks the source
/// again.
TEST_F(PictureCacheTest, AnAgedEntryIsAMissAndGoes)
{
    const PictureCache cache{root};
    ASSERT_EQ(cache.store(QStringLiteral("garden trowel"), {candidate()}, {jpegBytes()}).size(), 1);

    const auto directory = root / lens::pictures::cacheKey(QStringLiteral("garden trowel"), PictureCache::kThumbWidth).toStdWString();
    const auto metaFile  = directory / L"meta.json";

    QJsonObject meta;
    {
        QFile in(QString::fromStdWString(metaFile.wstring()));
        ASSERT_TRUE(in.open(QIODevice::ReadOnly));
        meta = QJsonDocument::fromJson(in.readAll()).object();
    }
    // Backdated rather than waited out: the TTL is 30 days.
    meta.insert(QStringLiteral("fetchedAt"), static_cast<double>(1));
    {
        QFile out(QString::fromStdWString(metaFile.wstring()));
        ASSERT_TRUE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    }

    EXPECT_FALSE(cache.load(QStringLiteral("garden trowel")).has_value());
    EXPECT_FALSE(std::filesystem::exists(directory)) << "the aged entry was left on disk";
}

/// The About page reads one number and clears the whole directory in one action: the number is
/// every file the cache holds, and clearing leaves the next open a miss (UI-SPEC.md 4.4).
TEST_F(PictureCacheTest, ReportsItsFootprintAndClearsEveryEntry)
{
    const PictureCache cache{root};
    EXPECT_EQ(cache.bytesOnDisk(), 0);
    cache.clear(); // an absent root is not an error
    EXPECT_EQ(cache.bytesOnDisk(), 0);

    ASSERT_EQ(cache.store(QStringLiteral("garden trowel"), {candidate()}, {jpegBytes()}).size(), 1);
    const qint64 held = cache.bytesOnDisk();
    EXPECT_GE(held, static_cast<qint64>(jpegBytes().size())) << "the stored picture is not counted";

    cache.clear();
    EXPECT_EQ(cache.bytesOnDisk(), 0);
    EXPECT_FALSE(cache.load(QStringLiteral("garden trowel")).has_value());
}

/// Metadata naming an image that is not there is not an entry: the card is never handed a path it
/// cannot draw.
TEST_F(PictureCacheTest, AnEntryWhosePictureIsGoneIsAMissAndGoes)
{
    const PictureCache cache{root};
    const auto         stored = cache.store(QStringLiteral("garden trowel"), {candidate()}, {jpegBytes()});
    ASSERT_EQ(stored.size(), 1);
    ASSERT_TRUE(std::filesystem::remove(std::filesystem::path{stored.front().file.toStdWString()}));

    EXPECT_FALSE(cache.load(QStringLiteral("garden trowel")).has_value());
    EXPECT_FALSE(std::filesystem::exists(root / lens::pictures::cacheKey(QStringLiteral("garden trowel"), PictureCache::kThumbWidth).toStdWString()));
}