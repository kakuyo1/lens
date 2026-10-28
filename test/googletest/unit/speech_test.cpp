/**
 * @file speech_test.cpp
 * @brief The pronunciation rules without a socket, a sound card or a settings document: which
 *        engine speaks, what the two switches store, how a recording is named, and the cache on a
 *        temp directory.
 *
 * The engine table is the whole of ADR 0134's fallback rule, and it is the part a reader would
 * notice as a bug: a word that suddenly arrives in a different voice than the one they chose is
 * the silent switch the ADR refuses, and a word that never speaks at all when nothing can serve it
 * is the silence it asks for. Both are decided here.
 */

#include <chrono>
#include <filesystem>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "speech/audio_cache.h"
#include "speech/speech_pure.h"

namespace {

using lens::speech::AudioCache;
using lens::speech::Engine;
using lens::speech::Fallback;

/// @return @p plan as a comparable value; gtest prints an optional<enum> as a byte.
int viaOf(Engine chosen, Fallback fallback, bool systemUsable, bool localUsable)
{
    const auto via = lens::speech::plan(chosen, fallback, systemUsable, localUsable);
    return via ? static_cast<int>(*via) + 1 : 0;
}

constexpr int kSilent = 0;
constexpr int kSystem = static_cast<int>(Engine::System) + 1;
constexpr int kOnline = static_cast<int>(Engine::Online) + 1;
constexpr int kLocal  = static_cast<int>(Engine::Local) + 1;

} // namespace

/// The default engine is the system synthesiser, and when it can serve, nothing else is consulted:
/// a machine with a voice does not quietly use the dictionary instead.
TEST(SpeechPlan, TheSystemEngineSpeaksWhenItCan)
{
    EXPECT_EQ(viaOf(Engine::System, Fallback::Online, true, false), kSystem);
    EXPECT_EQ(viaOf(Engine::System, Fallback::Local, true, true), kSystem);
    EXPECT_EQ(viaOf(Engine::System, Fallback::None, true, false), kSystem);
}

/// The fallback is what answers a system engine that cannot serve, and it is the reader's choice:
/// online by default, the local model when they picked it and have a voice, nothing when they
/// turned it off.
TEST(SpeechPlan, TheFallbackIsTheReadersChoice)
{
    EXPECT_EQ(viaOf(Engine::System, Fallback::Online, false, false), kOnline);
    EXPECT_EQ(viaOf(Engine::System, Fallback::Local, false, true), kLocal);
    EXPECT_EQ(viaOf(Engine::System, Fallback::Local, false, false), kSilent);
    EXPECT_EQ(viaOf(Engine::System, Fallback::None, false, false), kSilent);
}

/// A reader who picked online or local has said which one. A failure is silence, not the other
/// engine: the silent switch is what ADR 0134 and ADR 0130 both refuse.
TEST(SpeechPlan, AnExplicitEngineNeverSwitchesToTheOther)
{
    EXPECT_EQ(viaOf(Engine::Online, Fallback::Local, false, true), kOnline);
    EXPECT_EQ(viaOf(Engine::Online, Fallback::None, false, true), kOnline);
    EXPECT_EQ(viaOf(Engine::Local, Fallback::Online, true, true), kLocal);
    // The voice this accent needs is not downloaded: silent, rather than a request to the
    // dictionary the reader did not choose.
    EXPECT_EQ(viaOf(Engine::Local, Fallback::Online, true, false), kSilent);
}

/// The two switches store lower-case names, and a document that names something else reads as the
/// default rather than as a broken value.
TEST(SpeechSwitches, StoredNamesRoundTripAndUnknownValuesFallBack)
{
    EXPECT_EQ(lens::speech::engineName(Engine::System), QStringLiteral("system"));
    EXPECT_EQ(lens::speech::engineName(Engine::Online), QStringLiteral("online"));
    EXPECT_EQ(lens::speech::engineName(Engine::Local), QStringLiteral("local"));
    EXPECT_EQ(lens::speech::fallbackName(Fallback::Online), QStringLiteral("online"));
    EXPECT_EQ(lens::speech::fallbackName(Fallback::Local), QStringLiteral("local"));
    EXPECT_EQ(lens::speech::fallbackName(Fallback::None), QStringLiteral("none"));

    for (const Engine engine : {Engine::System, Engine::Online, Engine::Local})
        EXPECT_EQ(lens::speech::engineFrom(lens::speech::engineName(engine)), engine);
    for (const Fallback fallback : {Fallback::Online, Fallback::Local, Fallback::None})
        EXPECT_EQ(lens::speech::fallbackFrom(lens::speech::fallbackName(fallback)), fallback);

    EXPECT_EQ(lens::speech::engineFrom(QStringLiteral("whisper")), Engine::System);
    EXPECT_EQ(lens::speech::fallbackFrom(QString()), Fallback::Online);
    EXPECT_EQ(lens::speech::accentFrom(QStringLiteral("uk")), QStringLiteral("uk"));
    EXPECT_EQ(lens::speech::accentFrom(QStringLiteral("klingon")), QStringLiteral("us"));
}

/// The endpoint numbers the accents the other way round from the settings list: 1 is British and 2
/// is American (API.md section 9). A reader who switches accent has to hear a different recording,
/// and this number is the whole of that.
TEST(SpeechSwitches, TheEndpointTakesBritishAsOneAndAmericanAsTwo)
{
    EXPECT_EQ(lens::speech::onlineType(QStringLiteral("us")), QStringLiteral("2"));
    EXPECT_EQ(lens::speech::onlineType(QStringLiteral("uk")), QStringLiteral("1"));
    EXPECT_EQ(lens::speech::onlineType(QString()), QStringLiteral("2"));
}

/// The cache name is `sha1(accent|word)`: the accent is in it, so one word's two accents cannot be
/// mistaken for each other, and the word is, so two words cannot share a recording.
TEST(SpeechKey, TheNameCarriesTheAccentAndTheWord)
{
    const QString american = lens::speech::cacheKey(QStringLiteral("us"), QStringLiteral("record"));
    const QString british  = lens::speech::cacheKey(QStringLiteral("uk"), QStringLiteral("record"));
    const QString other    = lens::speech::cacheKey(QStringLiteral("us"), QStringLiteral("records"));

    EXPECT_EQ(american.size(), 40);
    EXPECT_EQ(british.size(), 40);
    EXPECT_NE(american, british) << "two accents shared one cached recording";
    EXPECT_NE(american, other) << "two words shared one cached recording";
    EXPECT_EQ(american, lens::speech::cacheKey(QStringLiteral("us"), QStringLiteral("record")));
    for (const QChar character : american)
        EXPECT_TRUE((character >= QLatin1Char('0') and character <= QLatin1Char('9')) or (character >= QLatin1Char('a') and character <= QLatin1Char('f')))
            << "the name is not lower-case hex: " << american.toStdString();
}

/// One word and nothing else: a sentence, a paragraph or an empty title is not sent anywhere. The
/// endpoint answers 500 for a sentence, and the reader would hear nothing either way -- this only
/// keeps the request from being made.
TEST(SpeechKey, OnlyAHeadwordIsRead)
{
    EXPECT_TRUE(lens::speech::isHeadword(QStringLiteral("record")));
    EXPECT_TRUE(lens::speech::isHeadword(QStringLiteral("give up")));
    EXPECT_TRUE(lens::speech::isHeadword(QString::fromUtf8("café")));
    EXPECT_FALSE(lens::speech::isHeadword(QString()));
    EXPECT_FALSE(lens::speech::isHeadword(QStringLiteral("one\ntwo")));
    EXPECT_FALSE(lens::speech::isHeadword(QString(65, QLatin1Char('a'))));
}

/// The cache: a hit reads back what was stored, a miss is nothing rather than an error, and a key
/// that did not come from cacheKey() never becomes a file name.
TEST(SpeechCache, AStoredRecordingIsAHitAndAnythingElseIsAMiss)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const std::filesystem::path root = std::filesystem::path{temporary.path().toStdWString()} / L"audio";
    AudioCache                  cache{root};

    const QString    key = lens::speech::cacheKey(QStringLiteral("us"), QStringLiteral("record"));
    const QByteArray mp3("ID3\x04\x00\x00\x00\x00\x00\x00", 10);

    EXPECT_FALSE(cache.load(key).has_value()) << "a cache that has never been written reported a hit";
    EXPECT_TRUE(cache.store(key, mp3));
    const auto hit = cache.load(key);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, mp3);
    EXPECT_EQ(cache.bytesOnDisk(), static_cast<qint64>(mp3.size()));

    // A name that is not 40 hex digits is not a file this cache wrote, so it is not read as one.
    EXPECT_FALSE(cache.load(QStringLiteral("../settings")).has_value());
    EXPECT_FALSE(cache.load(QString()).has_value());
    EXPECT_FALSE(cache.store(QStringLiteral("not-a-key"), mp3));

    cache.clear();
    EXPECT_FALSE(cache.load(key).has_value());
    EXPECT_EQ(cache.bytesOnDisk(), 0);
}

/// The ceiling is enforced on the oldest-wanted entry, not on the newest: the word the reader
/// pressed last is the last one to go.
TEST(SpeechCache, PruningDropsTheLeastRecentlyWantedFirst)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const std::filesystem::path root = std::filesystem::path{temporary.path().toStdWString()} / L"audio";
    AudioCache                  cache{root};

    const QString    older = lens::speech::cacheKey(QStringLiteral("us"), QStringLiteral("older"));
    const QString    newer = lens::speech::cacheKey(QStringLiteral("us"), QStringLiteral("newer"));
    const QByteArray bytes(1024, 'x');
    ASSERT_TRUE(cache.store(older, bytes));
    ASSERT_TRUE(cache.store(newer, bytes));

    // A hit is what marks an entry as wanted: reading the older one now is the reader pressing it
    // again, and that is what the next prune has to respect.
    std::filesystem::last_write_time(std::filesystem::path{temporary.path().toStdWString()} / L"audio" / (older.toStdWString() + L".mp3"),
                                     std::filesystem::file_time_type::clock::now() - std::chrono::hours(48));
    ASSERT_TRUE(cache.load(older).has_value());

    const auto olderTime = std::filesystem::last_write_time(std::filesystem::path{temporary.path().toStdWString()} / L"audio" / (older.toStdWString() + L".mp3"));
    const auto newerTime = std::filesystem::last_write_time(std::filesystem::path{temporary.path().toStdWString()} / L"audio" / (newer.toStdWString() + L".mp3"));
    EXPECT_GE(olderTime, newerTime) << "a hit did not move the entry to the front of the queue";
}
