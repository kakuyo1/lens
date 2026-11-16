/**
 * @file speech_duty_test.cpp
 * @brief The press path without a sound card: which engine a press reaches, what the cache saves,
 *        and which answer is allowed to play (docs/adr/0183).
 *
 * The output is a fake that records what it was asked, the recording source answers through the
 * scripted network manager, and the cache lives on a temporary directory. The engines' own wiring
 * -- the player's buffer lifetime, WinRT before SAPI -- is what the machine adapter carries and
 * what the manual run checks; what is pinned here is the duty's decisions around them.
 */

#include <filesystem>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QVariant>
#include <gtest/gtest.h>

#include "app/duties/speech_duty.h"
#include "app/duties/storage_duty.h"
#include "app/settings_access.h"
#include "core/document/known_store.h"
#include "core/domain/settings_table.h"
#include "scripted_manager.h"
#include "speech/audio_cache.h"
#include "speech/online_sources.h"
#include "speech/speech_pure.h"
#include "speech/voice_catalog.h"
#include "speech/voice_output.h"

namespace {

using lens::app::SpeechDuty;
using lens::app::StorageDuty;
using lens::speech::OnlineSource;
using lens::speech::OnlineSources;
using lens::speech::VoiceOutput;
using lens::test::ScriptedManager;

/// What the dictionary answers with: the source only checks that there are bytes.
const QByteArray kMp3("ID3\x04\x00\x00\x00\x00\x00\x00", 10);
/// What the local worker answers with; the player is never reached here.
const QByteArray kWav("RIFF....WAVE", 12);

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "speech_duty_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

/// The output a case drives: it records what it was asked and answers where a machine would.
class SilentVoiceOutput final : public VoiceOutput {
public:
    QString voice; ///< systemVoice()'s answer; empty means no engine serves the accent.
    bool    packInstalled = false;

    std::vector<QString>    said;
    std::vector<QString>    saidAccents;
    std::vector<QByteArray> played;
    int                     stops = 0;
    QString                 localWord;
    QString                 localAccent;
    QString                 localVoiceId;
    LocalSink               localSink;
    int                     localReleases = 0;

    QString systemVoice(QString const&) override
    {
        return voice;
    }
    bool installed(QString const&) override
    {
        return packInstalled;
    }
    bool say(QString const& word, QString const& accent) override
    {
        if (voice.isEmpty())
            return false;
        said.push_back(word);
        saidAccents.push_back(accent);
        return true;
    }
    bool play(QByteArray const& bytes) override
    {
        played.push_back(bytes);
        return true;
    }
    void speakLocal(QString const& word, QString const& accent, QString const& voiceId, LocalSink sink) override
    {
        localWord    = word;
        localAccent  = accent;
        localVoiceId = voiceId;
        localSink    = std::move(sink);
    }
    void stop() override
    {
        ++stops;
    }
    void releaseLocalVoice(QString const&) override
    {
        ++localReleases;
    }
};

/// @return The loop spun until @p done() or a second passed: the scripted reply finishes on the
///         next turn of the loop.
template <typename Done>
bool spinUntil(Done done)
{
    QElapsedTimer clock;
    clock.start();
    while (not done() and clock.elapsed() < 1000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct SpeechDutyTest : ::testing::Test {
    void SetUp() override
    {
        std::filesystem::remove(knownPath);
    }
    void TearDown() override
    {
        std::filesystem::remove(knownPath);
    }

    /// @brief Store one speech key through the row's own rule, as the settings page would.
    void set(char const* key, QVariant const& value)
    {
        const auto* row = lens::core::settings::find(key);
        ASSERT_NE(row, nullptr) << key;
        ASSERT_TRUE(lens::app::settings::write(storage.section(), *row, value)) << key;
    }

    QtApplication            qt;
    std::filesystem::path    knownPath = std::filesystem::temp_directory_path() / "lens_speech_duty_test.json";
    ScriptedManager          manager{200, {}, true, kMp3};
    OnlineSources            sources{&manager};
    QTemporaryDir            cacheDir;
    lens::speech::AudioCache cache{std::filesystem::path{cacheDir.path().toStdWString()} / L"audio"};
    lens::core::KnownStore   store = lens::core::KnownStore::load(knownPath);
    StorageDuty              storage{store};
    SilentVoiceOutput        output;
    SpeechDuty               duty{sources, cache, output, storage};
};

} // namespace

/// A sentence, a paragraph or an empty title is not a headword: nothing is said, nothing is fetched.
TEST_F(SpeechDutyTest, OnlyAHeadwordIsRead)
{
    output.voice = QStringLiteral("Microsoft Zira");

    duty.speak(QStringLiteral("one\ntwo"));

    EXPECT_TRUE(output.said.empty()) << "a sentence reached the synthesiser";
    EXPECT_TRUE(output.played.empty());
    EXPECT_EQ(manager.requests, 0) << "a sentence reached the dictionary";
    EXPECT_FALSE(duty.playing());
}

/// The reader pressing the speaker twice wants the word again, not two sounds at once: every press
/// silences what came before, and the icon follows the sound's own end.
TEST_F(SpeechDutyTest, APressSilencesThePreviousSoundAndTheIconFollows)
{
    output.voice = QStringLiteral("Microsoft Zira");

    duty.speak(QStringLiteral("bank"));
    ASSERT_EQ(output.said.size(), 1u);
    EXPECT_TRUE(duty.playing());
    EXPECT_EQ(duty.playingWord(), QStringLiteral("bank"));

    duty.speak(QStringLiteral("record"));
    ASSERT_EQ(output.said.size(), 2u);
    EXPECT_EQ(output.said.back(), QStringLiteral("record"));
    EXPECT_EQ(output.stops, 2) << "each press stops what the last one left sounding";
    EXPECT_EQ(duty.playingWord(), QStringLiteral("record"));

    // The engine's own state: speaking relights the icon, ready puts it out.
    output.onSoundState(false, {});
    EXPECT_FALSE(duty.playing());
    EXPECT_TRUE(duty.playingWord().isEmpty());
    output.onSoundState(true, QStringLiteral("record"));
    EXPECT_TRUE(duty.playing());
    EXPECT_EQ(duty.playingWord(), QStringLiteral("record"));
}

/// The accent in force is what both the system voice and the recording are asked for.
TEST_F(SpeechDutyTest, TheAccentInForceReachesTheOutput)
{
    output.voice = QStringLiteral("Microsoft Hazel");
    set("speechAccent", QStringLiteral("uk"));

    duty.speak(QStringLiteral("bank"));

    ASSERT_EQ(output.saidAccents.size(), 1u);
    EXPECT_EQ(output.saidAccents.back(), QStringLiteral("uk"));
}

/// A machine whose voice pack cannot serve the accent uses the fallback the reader chose: online
/// here, and the recording is kept for the next press.
TEST_F(SpeechDutyTest, AnUnavailableSystemVoiceFallsBackToTheOnlineRecording)
{
    duty.speak(QStringLiteral("bank"));

    EXPECT_TRUE(output.said.empty()) << "a voice that does not exist was never the answer";
    ASSERT_TRUE(spinUntil([&] { return not output.played.empty(); }));
    EXPECT_EQ(output.played.back(), kMp3);
    EXPECT_EQ(duty.playingWord(), QStringLiteral("bank"));
    EXPECT_TRUE(cache.load(lens::speech::cacheKey(OnlineSource::Youdao, QStringLiteral("us"), QStringLiteral("bank"))).has_value())
        << "the recording was played but not kept";
}

/// A hit costs no request at all, which is what makes the second press on a word instant.
TEST_F(SpeechDutyTest, ACachedRecordingIsPlayedWithoutAskingTheSource)
{
    const QString key = lens::speech::cacheKey(OnlineSource::Youdao, QStringLiteral("us"), QStringLiteral("bank"));
    ASSERT_TRUE(cache.store(key, kMp3));

    duty.speak(QStringLiteral("bank"));

    ASSERT_EQ(output.played.size(), 1u);
    EXPECT_EQ(output.played.front(), kMp3);
    EXPECT_EQ(manager.requests, 0) << "a cache hit still asked the dictionary";
}

/// A recording that arrives after its card is gone is still worth keeping, and is played nowhere.
TEST_F(SpeechDutyTest, ARecordingForADroppedPressIsKeptButNotPlayed)
{
    const QString key = lens::speech::cacheKey(OnlineSource::Youdao, QStringLiteral("us"), QStringLiteral("bank"));

    duty.speak(QStringLiteral("bank"));
    duty.stop();

    ASSERT_TRUE(spinUntil([&] { return cache.load(key).has_value(); }));
    EXPECT_TRUE(output.played.empty()) << "a recording for a press that is gone was played anyway";
    EXPECT_FALSE(duty.playing());
}

/// A fallback turned off is silence, not the engine the reader did not choose (ADR 0134).
TEST_F(SpeechDutyTest, TheFallbackOffMeansSilence)
{
    set("speechFallback", QStringLiteral("none"));

    duty.speak(QStringLiteral("bank"));

    EXPECT_TRUE(output.said.empty());
    EXPECT_TRUE(output.played.empty());
    EXPECT_EQ(manager.requests, 0) << "a fallback turned off still reached the dictionary";
    EXPECT_FALSE(duty.playing());
}

/// The local engine answers from its worker; the answer is played on the duty's thread.
TEST_F(SpeechDutyTest, TheLocalVoiceSpeaksThroughTheOutputAndItsAnswerPlays)
{
    set("speechEngine", QStringLiteral("local"));
    output.packInstalled = true;

    duty.speak(QStringLiteral("bank"));

    ASSERT_FALSE(output.localWord.isEmpty()) << "the local engine never reached the output";
    EXPECT_EQ(output.localWord, QStringLiteral("bank"));
    EXPECT_EQ(output.localVoiceId, lens::speech::voiceSpec(lens::speech::VoiceModel::Kokoro).id);
    EXPECT_EQ(output.localAccent, QStringLiteral("us"));
    EXPECT_TRUE(output.played.empty()) << "something was played before the worker answered";

    ASSERT_TRUE(output.localSink);
    output.localSink(kWav);

    ASSERT_EQ(output.played.size(), 1u);
    EXPECT_EQ(output.played.front(), kWav);
    EXPECT_TRUE(duty.playing());
    EXPECT_EQ(duty.playingWord(), QStringLiteral("bank"));
}

/// A worker answer for a superseded press is dropped, the same rule the source's answer follows.
TEST_F(SpeechDutyTest, ALocalAnswerForASupersededPressIsDropped)
{
    set("speechEngine", QStringLiteral("local"));
    output.packInstalled = true;

    duty.speak(QStringLiteral("bank"));
    auto stale = output.localSink;
    ASSERT_TRUE(stale);
    duty.speak(QStringLiteral("record"));

    stale(kWav);

    EXPECT_TRUE(output.played.empty()) << "a recording for a press that is gone was played anyway";
}
