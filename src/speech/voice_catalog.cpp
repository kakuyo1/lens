/**
 * @file voice_catalog.cpp
 * @brief The local voice catalogue and its file checks, behind voice_catalog.h.
 */

#include "speech/voice_catalog.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace lens::speech {

VoiceModel voiceFrom(QString const& setting)
{
    return setting == QLatin1String("ljspeech") ? VoiceModel::LjSpeech : VoiceModel::Kokoro;
}

VoiceSpec const& voiceSpec(VoiceModel model)
{
    // Sizes and digests were read at the pinned revision. The LFS files carry the host's own SHA-256;
    // the three small text files are plain git blobs with none, so their digests are of the files as
    // downloaded and checked against the same revision.
    static const VoiceFile kokoroFiles[]{
        {QStringLiteral("model.int8.onnx"), 114203914, QStringLiteral("05d793700b0b507f760c183c0e39bdd6a97b43d32fdc7bf7b54a5179f1b11222")},
        {QStringLiteral("voices.bin"), 53790720, QStringLiteral("e64a5a581d8c2a350d848f51c3121657cd83aa07ed6109172177345874a7244c")},
        {QStringLiteral("tokens.txt"), 1111, QStringLiteral("931ab2df2400cd65d580a22402024c2347ced8ae9ea300e545144b1aacc48e14")},
        {QStringLiteral("lexicon-us-en.txt"), 5956885, QStringLiteral("7daaab53a181be9885b853a8582bf1838186317e5dadacbcef9c426d6fa0da14")},
        {QStringLiteral("lexicon-gb-en.txt"), 6366635, QStringLiteral("c4cbb37316f62210dff52718a7afcaae24f50c032cc75ab47ae67b831d1049e7")},
    };
    static const VoiceFile ljSpeechFiles[]{
        {QStringLiteral("vits-ljs.int8.onnx"), 37423560, QStringLiteral("b3aa88de0202a040d7d75bac9dbf1a59d81d60c7369046f32241afa7dec65cc1")},
        {QStringLiteral("lexicon.txt"), 3708181, QStringLiteral("bdccfc6da71c45c48e2e0056fcf0aab760577c5f959f6c1b5eb3e3e916fd5a0e")},
        {QStringLiteral("tokens.txt"), 1084, QStringLiteral("5fee2c6b238d712287f2ecb08f34a8a8b413bcb7390862ef6fb6fd6f0f8d3a17")},
    };
    static const VoiceSpec kokoro{QStringLiteral("kokoro"), QStringLiteral("csukuangfj/kokoro-int8-multi-lang-v1_1"), QStringLiteral("1355334be009ccce397060ee639a874137a128fa"), kokoroFiles};
    static const VoiceSpec ljSpeech{QStringLiteral("ljspeech"), QStringLiteral("csukuangfj/vits-ljs"), QStringLiteral("7ac337c834f318e45a34037cb3371cc3929187ff"), ljSpeechFiles};
    return model == VoiceModel::LjSpeech ? ljSpeech : kokoro;
}

bool serves(VoiceModel model, QString const& accent)
{
    return model == VoiceModel::Kokoro or accent == QLatin1String("us");
}

int speakerFor(VoiceModel model, QString const& accent)
{
    if (model == VoiceModel::LjSpeech)
        return 0;
    return accent == QLatin1String("uk") ? 2 : 0;
}

QString lexiconFor(VoiceModel model, QString const& accent)
{
    if (model == VoiceModel::LjSpeech)
        return QStringLiteral("lexicon.txt");
    return accent == QLatin1String("uk") ? QStringLiteral("lexicon-gb-en.txt") : QStringLiteral("lexicon-us-en.txt");
}

std::filesystem::path voiceRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path{base.toStdWString()} / L"voices";
}

bool installed(VoiceModel model, std::filesystem::path const& root)
{
    VoiceSpec const& spec = voiceSpec(model);
    for (VoiceFile const& file : spec.files) {
        std::error_code error;
        const auto      size = std::filesystem::file_size(root / spec.id.toStdWString() / file.name.toStdWString(), error);
        if (error or size != static_cast<std::uintmax_t>(file.size))
            return false;
    }
    return true;
}

bool verifyFile(QString const& path, VoiceFile const& file)
{
    QFile source{path};
    if (not source.open(QIODevice::ReadOnly) or source.size() != file.size)
        return false;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    while (not source.atEnd())
        hash.addData(source.read(1 << 20));
    return hash.result().toHex() == file.sha256.toLatin1();
}

QByteArray wavBytes(std::span<float const> samples, int sampleRate)
{
    const auto  dataBytes = static_cast<quint32>(samples.size() * sizeof(qint16));
    QByteArray  out;
    QDataStream stream{&out, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    // The canonical 44-byte header: PCM, mono, 16 bits, at the engine's own rate.
    stream.writeRawData("RIFF", 4);
    stream << static_cast<quint32>(36 + dataBytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32{16} << quint16{1} << quint16{1} << static_cast<quint32>(sampleRate) << static_cast<quint32>(sampleRate * 2)
           << quint16{2} << quint16{16};
    stream.writeRawData("data", 4);
    stream << dataBytes;
    for (float const sample : samples)
        stream << static_cast<qint16>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    return out;
}

}
