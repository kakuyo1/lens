/**
 * @file speech_duty.cpp
 * @brief The press path behind speech_duty.h: the plan, the cache and the press that owns the speaker.
 */

#include "speech_duty.h"

#include <QDir>
#include <QPointer>

#include <optional>

#include "app/settings_access.h"
#include "core/domain/settings_table.h"
#include "speech/speech_pure.h"
#include "speech/voice_catalog.h"
#include "speech/voice_output.h"
#include "util/log.h"

namespace lens::app {
namespace {

using speech::Engine;
using speech::Fallback;

/// @return One row's value as the document holds it, read through the row's own rule.
QVariant rowValue(StorageDuty& storage, char const* key)
{
    auto const* row = core::settings::find(key);
    if (row == nullptr)
        return {};
    return settings::read(storage.section(), *row);
}

/// @return The local voice an id names, or nothing for an id the catalogue does not hold.
std::optional<speech::VoiceModel> voiceNamed(QString const& id)
{
    for (speech::VoiceModel const model : {speech::VoiceModel::Kokoro, speech::VoiceModel::LjSpeech})
        if (speech::voiceSpec(model).id == id)
            return model;
    return std::nullopt;
}

} // namespace

SpeechDuty::SpeechDuty(speech::OnlineSources& sources, speech::AudioCache& cache, speech::VoiceOutput& output, StorageDuty& storage, QObject* parent)
    : QObject(parent),
      sources_(sources),
      cache_(cache),
      output_(output),
      storage_(storage)
{
    // The output reports the sound ending on its own -- the end of a clip, a synthesiser going
    // ready, a recording that would not decode -- and the speaker icon follows it. Cleared in the
    // destructor, because the output outlives this duty.
    output_.onSoundState = [this](bool playing, QString const& word) {
        setPlaying(playing, word);
    };
    connect(&downloader_, &speech::VoiceDownloader::progressed, this, [this](qint64 received, qint64 total) {
        const int percent = total > 0 ? static_cast<int>(received * 100 / total) : 0;
        if (percent == voicePercent_)
            return;
        voicePercent_ = percent;
        emit voicesChanged();
    });
    connect(&downloader_, &speech::VoiceDownloader::finished, this, [this](bool) { emit voicesChanged(); });
}

SpeechDuty::~SpeechDuty()
{
    // The output outlives this duty and may still hold a callback into it.
    output_.onSoundState = {};
    output_.stop();
    disconnect(&downloader_, nullptr, this, nullptr);
    downloader_.cancel();
}

QString SpeechDuty::accent() const
{
    return speech::accentFrom(rowValue(storage_, "speechAccent").toString());
}

bool SpeechDuty::systemAvailable()
{
    return not output_.systemVoice(accent()).isEmpty();
}

QString SpeechDuty::systemVoice()
{
    return output_.systemVoice(accent());
}

bool SpeechDuty::localAvailable() const
{
    const auto model = speech::voiceFrom(rowValue(storage_, "speechLocalEngine").toString());
    return speech::serves(model, accent()) and output_.installed(speech::voiceSpec(model).id);
}

void SpeechDuty::downloadVoice(QString const& id)
{
    const auto model = voiceNamed(id);
    if (not model or downloader_.busy())
        return;
    voicePercent_ = 0;
    downloader_.start(*model, speech::voiceRoot());
    emit voicesChanged();
}

void SpeechDuty::cancelVoiceDownload()
{
    downloader_.cancel();
}

void SpeechDuty::removeVoice(QString const& id)
{
    const auto model = voiceNamed(id);
    if (not model or downloader_.busy())
        return;
    // The engine has to be released before its files are deleted, and the worker has to finish with it first,
    // so the next press loads the voice afresh if it is back.
    output_.releaseLocalVoice(id);
    QDir{QString::fromStdWString((speech::voiceRoot() / id.toStdWString()).wstring())}.removeRecursively();
    emit voicesChanged();
}

QVariantMap SpeechDuty::voiceStates() const
{
    QVariantMap states;
    for (speech::VoiceModel const model : {speech::VoiceModel::Kokoro, speech::VoiceModel::LjSpeech}) {
        const bool downloading = downloader_.busy() and downloader_.model() == model;
        states.insert(speech::voiceSpec(model).id, QVariantMap{
                                                       {QStringLiteral("installed"), speech::installed(model, speech::voiceRoot())},
                                                       {QStringLiteral("downloading"), downloading},
                                                       {QStringLiteral("percent"), downloading ? voicePercent_ : 0},
                                                   });
    }
    return states;
}

bool SpeechDuty::playing() const
{
    return playing_;
}

QString SpeechDuty::playingWord() const
{
    return playingWord_;
}

qint64 SpeechDuty::cacheBytes() const
{
    return cache_.bytesOnDisk();
}

void SpeechDuty::clearCache()
{
    cache_.clear();
    emit cacheBytesChanged();
}

void SpeechDuty::setPlaying(bool playing, QString const& word)
{
    const QString next = playing ? word : QString{};
    if (playing_ == playing and playingWord_ == next)
        return;
    playing_     = playing;
    playingWord_ = next;
    emit playingChanged();
}

void SpeechDuty::stop()
{
    ++requestId_;
    output_.stop();
    setPlaying(false);
}

void SpeechDuty::speakSystem(QString const& word)
{
    if (not output_.say(word, accent()))
        return;
    // Lit at the press, not at the engine's first audio: the reader pressed a button and the icon is
    // its answer. The output takes over from here, including a failure that stops it early.
    setPlaying(true, word);
}

void SpeechDuty::speak(QString const& word)
{
    const QString headword = word.trimmed();
    stop();
    if (not speech::isHeadword(headword)) {
        LENS_TRACE("pronunciation: '{}' is not a single word; nothing is read", headword.toStdString());
        return;
    }

    const auto chosen   = speech::engineFrom(rowValue(storage_, "speechEngine").toString());
    const auto fallback = speech::fallbackFrom(rowValue(storage_, "speechFallback").toString());
    const auto via      = speech::plan(chosen, fallback, systemAvailable(), localAvailable());
    if (not via) {
        // Nothing can serve this word. The card stays exactly as it was: no error, no notice.
        LENS_TRACE("pronunciation: no engine can read '{}'", headword.toStdString());
        return;
    }

    switch (*via) {
        case Engine::System:
            speakSystem(headword);
            return;
        case Engine::Local:
            // plan() names this only when the chosen voice is on this machine and serves the accent.
            speakLocal(headword, accent());
            return;
        case Engine::Online:
            playOnline(headword, accent());
            return;
    }
}

void SpeechDuty::speakLocal(QString const& word, QString const& accent) // NOLINT(bugprone-easily-swappable-parameters): speakLocal takes its 2 const QString & arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    const auto model = speech::voiceFrom(rowValue(storage_, "speechLocalEngine").toString());
    const int  press = requestId_;
    // The output answers from its worker thread, so a press replaced in the meantime is dropped
    // here -- the same rule the source's answer below follows. The guard keeps a callback posted
    // after this duty is gone from arriving at it.
    output_.speakLocal(word, accent, speech::voiceSpec(model).id, [guard = QPointer<SpeechDuty>(this), press, word](QByteArray const& wav) {
        if (not guard or press != guard->requestId_)
            return;
        guard->playBytes(word, wav);
    });
}

void SpeechDuty::playOnline(QString const& word, QString const& accent)
{
    // The source is the reader's choice, read per press: a recording from the previous source is not played.
    const auto    source = speech::onlineSourceFrom(rowValue(storage_, "speechOnlineProvider").toString());
    const QString key    = speech::cacheKey(source, accent, word);
    if (key.isEmpty())
        return;

    if (auto cached = cache_.load(key)) {
        // A hit costs no request at all, which is what makes the second press on a word instant.
        playBytes(word, *cached);
        return;
    }

    const int press = requestId_;
    sources_.pick(source).fetch(word, accent, [this, press, key, word](QByteArray const& bytes, bool ok) {
        if (not ok or bytes.isEmpty())
            return;
        // Stored whatever the reader is looking at now: a recording is worth keeping even when the
        // card that asked for it has gone.
        cache_.store(key, bytes);
        emit cacheBytesChanged();
        if (press != requestId_)
            return;
        playBytes(word, bytes);
    });
}

void SpeechDuty::playBytes(QString const& word, QByteArray const& bytes)
{
    if (not output_.play(bytes))
        return;
    setPlaying(true, word);
}

}
