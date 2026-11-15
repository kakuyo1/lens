/**
 * @file commons_source_test.cpp
 * @brief The Wikimedia Commons source over the scripted network: the search's address and answer,
 *        the thumbnail fetch that only an upload host may serve, and the silences a broken answer
 *        is read as.
 *
 * The parser's own shapes live in picture_pure_test.cpp; what this drives is the source around it
 * -- the request that goes out, the ceiling that cuts a body off, and the host the bytes may come
 * from.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <functional>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "pictures/picture_source.h"
#include "pictures/picture_sources/commons_source.h"
#include "scripted_manager.h"

namespace {

using lens::pictures::ByteSink;
using lens::pictures::CommonsSource;
using lens::pictures::Picture;
using lens::pictures::PictureSink;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "commons_source_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

bool spinUntil(const std::function<bool()>& done, int ms = 2000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// @return The decoded value of one query parameter.
QString queryValue(QUrl const& url, QString const& name)
{
    return QUrlQuery{url}.queryItemValue(name);
}

/// One Commons page that passes the source's own filter: a landscape photograph with a licence.
/// Built through QJson rather than written out, so the nesting is the compiler's problem.
const QByteArray kSearchBody = [] {
    const QJsonObject image{{"mime", "image/jpeg"},
                            {"width", 1200},
                            {"height", 800},
                            {"descriptionurl", "https://commons.wikimedia.org/wiki/File:River_bridge.jpg"},
                            {"thumburl", "https://upload.wikimedia.org/w/thumb.jpg"},
                            {"extmetadata", QJsonObject{{"LicenseShortName", QJsonObject{{"value", "CC BY-SA 4.0"}}}, {"LicenseUrl", QJsonObject{{"value", "https://creativecommons.org/licenses/by-sa/4.0"}}}, {"Artist", QJsonObject{{"value", "<span>A. Photographer</span>"}}}, {"ImageDescription", QJsonObject{{"value", "<span>A stone bridge over the river</span>"}}}}}};
    const QJsonObject page{{"ns", 6}, {"title", "File:River bridge.jpg"}, {"index", 1}, {"imageinfo", QJsonArray{image}}};
    return QJsonDocument{QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}}}.toJson(QJsonDocument::Compact);
}();

/// What the sink was handed; one call is the contract, whatever it answers.
struct SearchAnswer {
    int              calls = 0;
    QVector<Picture> pictures;
    bool             ok = false;
};

struct BytesAnswer {
    int        calls = 0;
    QByteArray bytes;
    bool       ok = false;
};

} // namespace

TEST(CommonsSource, TheSearchAddressCarriesTheQueryAndTheFilter)
{
    const QUrl url = CommonsSource::searchUrl(QStringLiteral("river bridge"));
    EXPECT_EQ(url.host(), QStringLiteral("commons.wikimedia.org"));
    EXPECT_EQ(url.path(), QStringLiteral("/w/api.php"));
    EXPECT_EQ(queryValue(url, QStringLiteral("gsrsearch")), QStringLiteral("river bridge"));
    EXPECT_EQ(queryValue(url, QStringLiteral("gsrnamespace")), QStringLiteral("6"));
    EXPECT_EQ(queryValue(url, QStringLiteral("gsrlimit")), QStringLiteral("12"));
    EXPECT_EQ(queryValue(url, QStringLiteral("iiurlwidth")), QStringLiteral("480"));
    EXPECT_EQ(queryValue(url, QStringLiteral("formatversion")), QStringLiteral("2"));
}

TEST(CommonsSource, ASearchHandsTheUsablePicturesBack)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kSearchBody};
    CommonsSource   source{&manager};
    SearchAnswer    seen;

    source.search(QStringLiteral("river"), [&seen](QVector<Picture> const& pictures, bool ok) {
        ++seen.calls;
        seen.pictures = pictures;
        seen.ok       = ok;
    });
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_TRUE(seen.ok);
    ASSERT_EQ(seen.pictures.size(), 1);
    EXPECT_EQ(seen.pictures.at(0).author, QStringLiteral("A. Photographer"));
    EXPECT_EQ(seen.pictures.at(0).license, QStringLiteral("CC BY-SA 4.0"));
    EXPECT_EQ(seen.pictures.at(0).source, QStringLiteral("Wikimedia Commons"));
    EXPECT_EQ(seen.pictures.at(0).thumbnail.host(), QStringLiteral("upload.wikimedia.org"));
    // One GET of the API, the query the reader's word.
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].verb, "GET");
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("gsrsearch=river")));
}

TEST(CommonsSource, AnEmptyQueryIsRefusedWithoutARequest)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kSearchBody};
    CommonsSource   source{&manager};
    SearchAnswer    seen;

    source.search(QStringLiteral("   "), [&seen](QVector<Picture> const& pictures, bool ok) {
        ++seen.calls;
        seen.pictures = pictures;
        seen.ok       = ok;
    });
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
    EXPECT_TRUE(seen.pictures.isEmpty());
    EXPECT_EQ(manager.requests, 0);
}

TEST(CommonsSource, ARefusedSearchIsASilenceAndAnEmptyOneIsAnAnswer)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    CommonsSource   source{&manager};
    SearchAnswer    seen;

    source.search(QStringLiteral("river"), [&seen](QVector<Picture> const& pictures, bool ok) {
        ++seen.calls;
        seen.pictures = pictures;
        seen.ok       = ok;
    });
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));
    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok) << "a refused search never reads as found-nothing";

    // A search that answered with nothing usable reached the source and had nothing: an empty
    // list with ok true is that answer, and the card shows no strip for it.
    SearchAnswer empty;
    manager.queue(200, QByteArray("{}"));
    source.search(QStringLiteral("river"), [&empty](QVector<Picture> const& pictures, bool ok) {
        ++empty.calls;
        empty.pictures = pictures;
        empty.ok       = ok;
    });
    ASSERT_TRUE(spinUntil([&empty] { return empty.calls > 0; }));
    ASSERT_EQ(empty.calls, 1);
    EXPECT_TRUE(empty.ok);
    EXPECT_TRUE(empty.pictures.isEmpty());
}

TEST(CommonsSource, AFetchRefusesAHostThatIsNotAnUploadHost)
{
    // Only the upload hosts an answer may name: a redirect or a hand-edited cache entry pointing
    // anywhere else is refused here, so nothing downloads from a stranger.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray("pixels")};
    CommonsSource   source{&manager};
    BytesAnswer     seen;

    for (QUrl const& url : {QUrl{QStringLiteral("https://evil.example.com/thumb.jpg")},
                            QUrl{QStringLiteral("http://upload.wikimedia.org/w/thumb.jpg")}}) {
        source.fetch(url, [&seen](QByteArray const& bytes, bool ok) {
            ++seen.calls;
            seen.bytes = bytes;
            seen.ok    = ok;
        });
        ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));
        EXPECT_FALSE(seen.ok);
        seen.calls = 0;
        seen.ok    = false;
    }
    EXPECT_EQ(manager.requests, 0);
}

TEST(CommonsSource, AFetchHandsTheBytesBackFromAnUploadHost)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray("pixels")};
    CommonsSource   source{&manager};
    BytesAnswer     seen;

    source.fetch(QUrl{QStringLiteral("https://upload.wikimedia.org/w/thumb.jpg")},
                 [&seen](QByteArray const& bytes, bool ok) {
                     ++seen.calls;
                     seen.bytes = bytes;
                     seen.ok    = ok;
                 });
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_TRUE(seen.ok);
    EXPECT_EQ(seen.bytes, QByteArray("pixels"));
    // A redirect is not followed: the address checked above is the only one pixels come from.
    EXPECT_EQ(manager.redirectPolicy.toInt(), static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
}

TEST(CommonsSource, AFetchOfABrokenAnswerIsASilence)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    CommonsSource   source{&manager};
    BytesAnswer     seen;

    source.fetch(QUrl{QStringLiteral("https://upload.wikimedia.org/w/thumb.jpg")},
                 [&seen](QByteArray const& bytes, bool ok) {
                     ++seen.calls;
                     seen.bytes = bytes;
                     seen.ok    = ok;
                 });
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));
    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);

    // An answer with no pixels in it is not a recording of nothing: it is a refusal too.
    BytesAnswer hollow;
    manager.queue(200);
    source.fetch(QUrl{QStringLiteral("https://upload.wikimedia.org/w/thumb.jpg")},
                 [&hollow](QByteArray const& bytes, bool ok) {
                     ++hollow.calls;
                     hollow.bytes = bytes;
                     hollow.ok    = ok;
                 });
    ASSERT_TRUE(spinUntil([&hollow] { return hollow.calls > 0; }));
    EXPECT_FALSE(hollow.ok);

    // And one byte past the ceiling stops the transfer there rather than filling the disk.
    BytesAnswer huge;
    manager.queue(200, QByteArray(static_cast<int>(2 * 1024 * 1024) + 1, 'x'));
    source.fetch(QUrl{QStringLiteral("https://upload.wikimedia.org/w/thumb.jpg")},
                 [&huge](QByteArray const& bytes, bool ok) {
                     ++huge.calls;
                     huge.bytes = bytes;
                     huge.ok    = ok;
                 });
    ASSERT_TRUE(spinUntil([&huge] { return huge.calls > 0; }));
    EXPECT_FALSE(huge.ok);
    EXPECT_EQ(manager.requests, 3) << "the oversized transfer was cut off, not completed";
}
