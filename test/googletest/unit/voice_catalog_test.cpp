/**
 * @file voice_catalog_test.cpp
 * @brief The local voice catalogue without a network or a sound card: which voice a setting names, which
 *        accent each one serves, the files a voice is complete with, and the WAV it is wrapped in.
 *
 * The catalogue is what decides whether a word is spoken in the accent the reader chose, and whether a
 * half-downloaded voice is ever loaded. Both are decided here.
 */

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QtEndian>
#include <filesystem>
#include <gtest/gtest.h>

#include "speech/voice_catalog.h"

namespace {

using lens::speech::VoiceFile;
using lens::speech::VoiceModel;

/// @brief Create a file of exactly @p size bytes. resize() extends it without writing the bytes.
void makeFile(QString const& path, qint64 size)
{
    QFile file{path};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(size));
}

/// @return The little-endian unsigned value at @p at, the way a WAV header states its fields.
quint32 u32At(QByteArray const& bytes, int at)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<uchar const*>(bytes.constData() + at));
}

quint16 u16At(QByteArray const& bytes, int at)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<uchar const*>(bytes.constData() + at));
}

}

TEST(VoiceCatalog, AutomaticAndAnythingUnknownIsKokoro)
{
    EXPECT_EQ(lens::speech::voiceFrom(QStringLiteral("auto")), VoiceModel::Kokoro);
    EXPECT_EQ(lens::speech::voiceFrom(QStringLiteral("kokoro")), VoiceModel::Kokoro);
    EXPECT_EQ(lens::speech::voiceFrom(QStringLiteral("")), VoiceModel::Kokoro);
    EXPECT_EQ(lens::speech::voiceFrom(QStringLiteral("nonsense")), VoiceModel::Kokoro);
    EXPECT_EQ(lens::speech::voiceFrom(QStringLiteral("ljspeech")), VoiceModel::LjSpeech);
}

TEST(VoiceCatalog, LjSpeechServesAmericanOnly)
{
    EXPECT_TRUE(lens::speech::serves(VoiceModel::Kokoro, QStringLiteral("us")));
    EXPECT_TRUE(lens::speech::serves(VoiceModel::Kokoro, QStringLiteral("uk")));
    EXPECT_TRUE(lens::speech::serves(VoiceModel::LjSpeech, QStringLiteral("us")));
    EXPECT_FALSE(lens::speech::serves(VoiceModel::LjSpeech, QStringLiteral("uk")));
}

TEST(VoiceCatalog, SpeakersAreTheOnesTheVoicesFileNumbers)
{
    // sherpa-onnx's v1.1 order: 0 is af_maple (American), 2 is bf_vale (British).
    EXPECT_EQ(lens::speech::speakerFor(VoiceModel::Kokoro, QStringLiteral("us")), 0);
    EXPECT_EQ(lens::speech::speakerFor(VoiceModel::Kokoro, QStringLiteral("uk")), 2);
    EXPECT_EQ(lens::speech::speakerFor(VoiceModel::LjSpeech, QStringLiteral("us")), 0);
}

TEST(VoiceCatalog, EveryLexiconAVoiceReadsWithIsOneOfItsFiles)
{
    for (VoiceModel const model : {VoiceModel::Kokoro, VoiceModel::LjSpeech}) {
        for (QString const accent : {QStringLiteral("us"), QStringLiteral("uk")}) {
            if (not lens::speech::serves(model, accent))
                continue;
            const QString lexicon = lens::speech::lexiconFor(model, accent);
            bool          listed  = false;
            for (VoiceFile const& file : lens::speech::voiceSpec(model).files)
                listed = listed or file.name == lexicon;
            EXPECT_TRUE(listed) << lexicon.toStdString();
        }
    }
}

TEST(VoiceCatalog, InstalledNeedsEveryFileAtItsExactSize)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const std::filesystem::path root{temp.path().toStdWString()};
    const auto&                 spec   = lens::speech::voiceSpec(VoiceModel::LjSpeech);
    const QString               folder = temp.path() + QLatin1Char('/') + spec.id;
    ASSERT_TRUE(QDir{}.mkpath(folder));

    // One file missing: not installed.
    for (std::size_t index = 1; index < spec.files.size(); ++index)
        makeFile(folder + QLatin1Char('/') + spec.files[index].name, spec.files[index].size);
    EXPECT_FALSE(lens::speech::installed(VoiceModel::LjSpeech, root));

    // All present at their sizes: installed.
    makeFile(folder + QLatin1Char('/') + spec.files[0].name, spec.files[0].size);
    EXPECT_TRUE(lens::speech::installed(VoiceModel::LjSpeech, root));

    // One a byte short, as a cut-off transfer would leave it: not installed.
    makeFile(folder + QLatin1Char('/') + spec.files[0].name, spec.files[0].size - 1);
    EXPECT_FALSE(lens::speech::installed(VoiceModel::LjSpeech, root));
}

TEST(VoiceCatalog, VerifyFileChecksTheSizeAndTheDigest)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString path = temp.path() + QStringLiteral("/hello.txt");
    {
        QFile file{path};
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("hello"), 5);
    }
    // SHA-256 of "hello".
    const QString digest = QStringLiteral("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");

    EXPECT_TRUE(lens::speech::verifyFile(path, VoiceFile{QStringLiteral("hello.txt"), 5, digest}));
    EXPECT_FALSE(lens::speech::verifyFile(path, VoiceFile{QStringLiteral("hello.txt"), 6, digest}));
    EXPECT_FALSE(lens::speech::verifyFile(path, VoiceFile{QStringLiteral("hello.txt"), 5, QStringLiteral("00")}));
}

TEST(VoiceCatalog, WavIsSixteenBitMonoAtTheEnginesRate)
{
    const float      samples[]{0.5f, -1.0f, 2.0f};
    const QByteArray wav = lens::speech::wavBytes(samples, 24000);

    ASSERT_EQ(wav.size(), 44 + 3 * 2);
    EXPECT_EQ(wav.mid(0, 4), QByteArray{"RIFF"});
    EXPECT_EQ(u32At(wav, 4), 36u + 6u);
    EXPECT_EQ(wav.mid(8, 4), QByteArray{"WAVE"});
    EXPECT_EQ(wav.mid(12, 4), QByteArray{"fmt "});
    EXPECT_EQ(u32At(wav, 16), 16u);
    EXPECT_EQ(u16At(wav, 20), 1);      // PCM
    EXPECT_EQ(u16At(wav, 22), 1);      // mono
    EXPECT_EQ(u32At(wav, 24), 24000u); // rate
    EXPECT_EQ(u32At(wav, 28), 48000u); // bytes a second
    EXPECT_EQ(u16At(wav, 32), 2);      // bytes a frame
    EXPECT_EQ(u16At(wav, 34), 16);     // bits
    EXPECT_EQ(wav.mid(36, 4), QByteArray{"data"});
    EXPECT_EQ(u32At(wav, 40), 6u);

    // Half of full scale rounds away from zero; anything past full scale is clipped.
    EXPECT_EQ(static_cast<qint16>(u16At(wav, 44)), 16384);
    EXPECT_EQ(static_cast<qint16>(u16At(wav, 46)), -32767);
    EXPECT_EQ(static_cast<qint16>(u16At(wav, 48)), 32767);
}
