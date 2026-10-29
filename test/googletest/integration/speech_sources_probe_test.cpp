/**
 * @file speech_sources_probe_test.cpp
 * @brief The online recording sources against their real endpoints: one word per source and accent, and the
 *        answer must be an mp3 (docs/adr/0138).
 *
 * Human-run, because it needs the network and the endpoints belong to other services:
 * `powershell -File scripts/qa/gtest.ps1 -Target lens_gtest_integration -Filter 'SpeechSourcesProbe*'`.
 */

#include <chrono>

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <gtest/gtest.h>

#include "speech/google_source.h"
#include "speech/youdao_source.h"

namespace {

using namespace std::chrono_literals;

/// @brief What one request answered: whether it came back at all, whether it was a recording, and its bytes.
struct Answer {
    bool       arrived = false;
    bool       ok      = false;
    QByteArray bytes;
};

/// @return The application object the network needs, made once for the process.
void ensureEventLoop()
{
    if (QCoreApplication::instance() != nullptr)
        return;
    static int              argc   = 1;
    static char             name[] = "lens-probe";
    static char*            argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
}

/// @brief Ask @p source for one word in one accent, and run the event loop until it answers or the wait runs out.
Answer ask(lens::speech::PronunciationSource& source, QString const& word, QString const& accent)
{
    ensureEventLoop();
    Answer answer;
    source.fetch(word, accent, [&answer](QByteArray const& bytes, bool ok) {
        answer.arrived = true;
        answer.ok      = ok;
        answer.bytes   = bytes;
    });
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (not answer.arrived and std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return answer;
}

/// @return Whether @p bytes start like an mp3: an ID3 tag, or an MPEG audio frame sync.
bool isMp3(QByteArray const& bytes)
{
    if (bytes.startsWith("ID3"))
        return true;
    return bytes.size() >= 2 and static_cast<unsigned char>(bytes[0]) == 0xFF and (static_cast<unsigned char>(bytes[1]) & 0xE0) == 0xE0;
}

void expectRecording(lens::speech::PronunciationSource& source, char const* accent)
{
    const Answer answer = ask(source, QStringLiteral("hello"), QString::fromLatin1(accent));
    ASSERT_TRUE(answer.arrived) << "no answer within the wait (" << accent << ")";
    ASSERT_TRUE(answer.ok) << "the source refused the word (" << accent << ")";
    EXPECT_TRUE(isMp3(answer.bytes)) << "the answer is not an mp3 (" << accent << "): " << answer.bytes.size() << " bytes";
    EXPECT_GT(answer.bytes.size(), 1000) << "suspiciously small recording (" << accent << ")";
}

}

TEST(SpeechSourcesProbe, YoudaoAnswersWithAnMp3InEachAccent)
{
    lens::speech::YoudaoSource source;
    expectRecording(source, "us");
    expectRecording(source, "uk");
}

TEST(SpeechSourcesProbe, GoogleAnswersWithAnMp3InEachAccent)
{
    lens::speech::GoogleSource source;
    expectRecording(source, "us");
    expectRecording(source, "uk");
}
