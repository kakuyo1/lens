/**
 * @file google_source_test.cpp
 * @brief The Google recording source, without a network: the voice is chosen by the language tag,
 *        the word is one query value and cannot add a parameter of its own, and every shape the
 *        endpoint answers -- a recording, an HTML refusal, an over-ceiling body -- lands in the
 *        sink the contract names.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

#include <gtest/gtest.h>

#include "scripted_manager.h"
#include "speech/google_source.h"

namespace {

using lens::speech::AudioSink;
using lens::speech::GoogleSource;
using lens::test::ScriptedManager;

QString queryValue(QUrl const& url, QString const& name)
{
    return QUrlQuery{url}.queryItemValue(name);
}

/// One QCoreApplication for a case's duration: the source's network objects need one, and Qt allows
/// one at a time, so each case that touches them makes its own.
struct QtApplication {
    QtApplication()
        : application(argc, argv)
    {
    }

    int              argc     = 1;
    char             name[24] = "google_source_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application;
};

/// What the sink was handed. One call is the contract: the source answers exactly once.
struct Answer {
    int        calls = 0;
    QByteArray bytes;
    bool       ok = false;
};

/// @return Whether @p done came true within @p ms, turning the loop so a reply can arrive.
bool spinUntil(const std::function<bool()>& done, int ms = 2000)
{
    QElapsedTimer clock;
    clock.start();
    while (not done() and clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

AudioSink record(Answer& seen)
{
    return [&seen](QByteArray const& bytes, bool ok) {
        ++seen.calls;
        seen.bytes = bytes;
        seen.ok    = ok;
    };
}

} // namespace

TEST(GoogleSource, TheLanguageTagPicksTheVoice)
{
    EXPECT_EQ(queryValue(lens::speech::GoogleSource::urlFor(QStringLiteral("hello"), QStringLiteral("us")), QStringLiteral("tl")),
              QStringLiteral("en-US"));
    EXPECT_EQ(queryValue(lens::speech::GoogleSource::urlFor(QStringLiteral("hello"), QStringLiteral("uk")), QStringLiteral("tl")),
              QStringLiteral("en-GB"));
}

TEST(GoogleSource, TheAddressIsTheFixedEndpoint)
{
    const QUrl url = lens::speech::GoogleSource::urlFor(QStringLiteral("hello"), QStringLiteral("us"));
    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("translate.google.com"));
    EXPECT_EQ(url.path(), QStringLiteral("/translate_tts"));
}

TEST(GoogleSource, TheWordIsOneValueAndCannotAddAParameter)
{
    // A word with an ampersand in it must stay the word: it cannot switch the voice or add a field.
    const QUrl url = lens::speech::GoogleSource::urlFor(QStringLiteral("a&tl=fr"), QStringLiteral("uk"));
    EXPECT_EQ(queryValue(url, QStringLiteral("q")), QStringLiteral("a&tl=fr"));
    EXPECT_EQ(queryValue(url, QStringLiteral("tl")), QStringLiteral("en-GB"));
}

TEST(GoogleSource, ARecordingArrivesAsBytesAndIsAnsweredOnce)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray(800, 'm'), QByteArrayLiteral("audio/mpeg")};
    GoogleSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("uk"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_TRUE(seen.ok);
    EXPECT_EQ(seen.bytes.size(), 800);
    // The request went to the endpoint with the word as one value, and a redirect is not followed:
    // the address checked here is the only one the word may travel to.
    ASSERT_EQ(manager.sent.size(), 1);
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("translate.google.com/translate_tts")));
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("q=hello")));
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("tl=en-GB")));
    EXPECT_EQ(manager.redirectPolicy.toInt(), static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
}

TEST(GoogleSource, AnAnswerThatIsNotAudioIsARefusalWhateverItsStatusSays)
{
    // The endpoint answers with HTML for a request it does not like, and that must not reach the
    // player: the content type is what refuses it, not the status.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray("<html>no</html>"), QByteArrayLiteral("text/html")};
    GoogleSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
    EXPECT_TRUE(seen.bytes.isEmpty());
}

TEST(GoogleSource, ARefusedStatusIsASilence)
{
    QtApplication   qt;
    ScriptedManager manager{500, {}, true, QByteArray("unavailable")};
    GoogleSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
}

TEST(GoogleSource, AnAnswerPastTheCeilingIsCutOff)
{
    // One byte over the ceiling and the transfer stops there: the reply aborts, and the abort reads
    // as a refusal rather than as a short recording.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray(static_cast<int>(GoogleSource::kMaxBytes) + 1, 'm'), QByteArrayLiteral("audio/mpeg")};
    GoogleSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
    EXPECT_EQ(manager.requests, 1);
}
