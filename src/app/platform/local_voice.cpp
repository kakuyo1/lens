/**
 * @file local_voice.cpp
 * @brief The sherpa-onnx calls behind local_voice.h.
 */

#include "app/platform/local_voice.h"

#include "c-api.h"
#include "util/log.h"

#include <span>

namespace lens::app {

LocalVoice::LocalVoice(speech::VoiceModel model, QString accent, std::filesystem::path root)
    : model_(model),
      accent_(std::move(accent)),
      root_(std::move(root))
{
}

LocalVoice::~LocalVoice()
{
    if (tts_ != nullptr)
        SherpaOnnxDestroyOfflineTts(tts_);
}

speech::VoiceModel LocalVoice::model() const
{
    return model_;
}

QString LocalVoice::accent() const
{
    return accent_;
}

bool LocalVoice::load()
{
    if (tts_ != nullptr)
        return true;

    // The byte arrays below are alive for the whole create call, which is all the C API needs of them.
    const QString    folder  = QString::fromStdWString((root_ / speech::voiceSpec(model_).id.toStdWString()).wstring());
    const auto       path    = [&folder](QString const& name) { return (folder + QLatin1Char('/') + name).toUtf8(); };
    const QByteArray tokens  = path(QStringLiteral("tokens.txt"));
    const QByteArray lexicon = path(speech::lexiconFor(model_, accent_));

    // Zeroed first, as sherpa's own examples do: the fields of the families this voice does not use
    // stay null.
    SherpaOnnxOfflineTtsConfig config{};
    config.model.num_threads = 2;
    config.model.provider    = "cpu";
    config.max_num_sentences = 1;

    QByteArray model;
    QByteArray voices;
    if (model_ == speech::VoiceModel::Kokoro) {
        model                            = path(QStringLiteral("model.int8.onnx"));
        voices                           = path(QStringLiteral("voices.bin"));
        config.model.kokoro.model        = model.constData();
        config.model.kokoro.voices       = voices.constData();
        config.model.kokoro.tokens       = tokens.constData();
        config.model.kokoro.lexicon      = lexicon.constData();
        config.model.kokoro.length_scale = 1.0f;
    } else {
        // The CLI's own defaults for the VITS sampling noise; zero would be a voice with none.
        model                           = path(QStringLiteral("vits-ljs.int8.onnx"));
        config.model.vits.model         = model.constData();
        config.model.vits.tokens        = tokens.constData();
        config.model.vits.lexicon       = lexicon.constData();
        config.model.vits.length_scale  = 1.0f;
        config.model.vits.noise_scale   = 0.667f;
        config.model.vits.noise_scale_w = 0.8f;
    }

    tts_ = SherpaOnnxCreateOfflineTts(&config);
    if (tts_ == nullptr) {
        LENS_WARN("voice '{}': the engine did not load", speech::voiceSpec(model_).id.toStdString());
        return false;
    }
    LENS_INFO("voice '{}' loaded for '{}'", speech::voiceSpec(model_).id.toStdString(), accent_.toStdString());
    return true;
}

std::optional<QByteArray> LocalVoice::speak(QString const& word)
{
    if (not load())
        return std::nullopt;

    const QByteArray           text = word.toUtf8();
    SherpaOnnxGenerationConfig generation{};
    generation.sid           = speech::speakerFor(model_, accent_);
    generation.speed         = 1.0f;
    generation.silence_scale = 0.2f;

    const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(tts_, text.constData(), &generation, nullptr, nullptr);
    if (audio == nullptr)
        return std::nullopt;

    std::optional<QByteArray> wav;
    if (audio->n > 0)
        wav = speech::wavBytes(std::span<float const>{audio->samples, static_cast<std::size_t>(audio->n)}, audio->sample_rate);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return wav;
}

}
