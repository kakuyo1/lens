/**
 * @file youdao_source_test.cpp
 * @brief The dictionary recording source, without a network: the address encodes the word and the
 *        accent and nothing else, and every shape the endpoint answers -- a recording, a refusal,
 *        an over-ceiling body -- lands in the sink the contract names.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

#include <gtest/gtest.h>

#include "scripted_manager.h"
#include "speech/youdao_source.h"

namespace {

using lens::speech::AudioSink;
using lens::speech::YoudaoSource;
using lens::test::ScriptedManager;

/// @return The decoded value of one query parameter.
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
    char             name[24] = "youdao_source_test";
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

TEST(YoudaoSource, TheAddressIsTheDictionaryEndpointWithTheAccentInTheType)
{
    // British is 1 and American is 2; the endpoint numbers them the other way round from the way the
    // settings list them, which speech_pure's onlineType() owns.
    const QUrl us = YoudaoSource::urlFor(QStringLiteral("hello"), QStringLiteral("us"));
    EXPECT_EQ(us.host(), QStringLiteral("dict.youdao.com"));
    EXPECT_EQ(us.path(), QStringLiteral("/dictvoice"));
    EXPECT_EQ(queryValue(us, QStringLiteral("type")), QStringLiteral("2"));
    EXPECT_EQ(queryValue(YoudaoSource::urlFor(QStringLiteral("hello"), QStringLiteral("uk")), QStringLiteral("type")),
              QStringLiteral("1"));
}

TEST(YoudaoSource, TheWordIsOneQueryValueWhateverItCarries)
{
    // A word with an ampersand or an apostrophe must stay the word: it cannot switch the type or add
    // a field of its own.
    const QUrl url = YoudaoSource::urlFor(QStringLiteral("it's a&b"), QStringLiteral("us"));
    EXPECT_EQ(queryValue(url, QStringLiteral("audio")), QStringLiteral("it's a&b"));
    EXPECT_EQ(queryValue(url, QStringLiteral("type")), QStringLiteral("2"));
}

TEST(YoudaoSource, ARecordingArrivesAsBytesAndIsAnsweredOnce)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray(1000, 'm')};
    YoudaoSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_TRUE(seen.ok);
    EXPECT_EQ(seen.bytes.size(), 1000);
    // The word and the accent, and nothing else, is what the request carried.
    ASSERT_EQ(manager.sent.size(), 1);
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("audio=hello")));
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("type=2")));
    // The endpoint answers where it is; a redirect would carry the word elsewhere, so it is not followed.
    EXPECT_EQ(manager.redirectPolicy.toInt(), static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
}

TEST(YoudaoSource, TheDictionaryRefusalIsASilenceNotAnError)
{
    // A sentence, and some phrases, answer 500 with a 120-byte body: this is a dictionary entry's
    // recording, not a synthesiser, and a refusal is answered the way every other refusal is.
    QtApplication   qt;
    ScriptedManager manager{500, {}, true, QByteArray(120, '<')};
    YoudaoSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("a sentence"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
    EXPECT_TRUE(seen.bytes.isEmpty());
}

TEST(YoudaoSource, AnEmptyAnswerIsARefusal)
{
    QtApplication   qt;
    ScriptedManager manager{200};
    YoudaoSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
}

TEST(YoudaoSource, AnAnswerPastTheCeilingIsCutOff)
{
    // One byte over the ceiling and the transfer stops there: the reply aborts, and the abort reads
    // as a refusal rather than as a short recording.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray(static_cast<int>(YoudaoSource::kMaxBytes) + 1, 'm')};
    YoudaoSource    source{&manager};
    Answer          seen;

    source.fetch(QStringLiteral("hello"), QStringLiteral("us"), record(seen));
    ASSERT_TRUE(spinUntil([&seen] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_FALSE(seen.ok);
    EXPECT_TRUE(seen.bytes.isEmpty());
    EXPECT_EQ(manager.requests, 1);
}
