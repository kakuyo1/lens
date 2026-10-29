/**
 * @file speech_duty.cpp
 * @brief The three engines behind speech_duty.h, and the player they all end at.
 */

#include "speech_duty.h"

#include <QAudioOutput>
#include <QDir>
#include <QMediaPlayer>
#include <QLocale>
#include <QRunnable>
#include <QTextToSpeech>
#include <QVoice>

#include <optional>

#include "app/platform/local_voice.h"
#include "app/settings_access.h"
#include "core/domain/settings_table.h"
#include "speech/speech_pure.h"
#include "speech/voice_catalog.h"
#include "util/log.h"

namespace lens::app {
namespace {

using speech::Engine;
using speech::Fallback;

/// How long the local engine stays loaded after the last press. Holding it costs about 390 MB and loading
/// it again about three seconds, so five minutes keeps a run of words instant without holding the memory
/// for the rest of the session.
constexpr int kLocalVoiceIdleMs = 5 * 60 * 1000;

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

/// @return The engines this build may speak through, in the order they are asked: WinRT's
///         synthesiser first, SAPI second. `mock` is Qt's own test engine and is never asked for --
///         it answers with a voice list and produces no sound, which would read as a broken speaker.
QStringList systemEngines()
{
    QStringList       ordered;
    const QStringList available = QTextToSpeech::availableEngines();
    for (const QString& name : {QStringLiteral("winrt"), QStringLiteral("sapi")})
        if (available.contains(name))
            ordered.append(name);
    return ordered;
}

/// @return The voice that speaks @p accent, or nothing when this engine's voice pack has none.
///
/// The accent is a territory, not a language: en-US and en-GB are two voices and a machine may have
/// either, both or neither. A missing one is "this engine cannot serve this accent" rather than a
/// reason to read American at a reader who asked for British (ADR 0134) -- plan() then sends the
/// word to the fallback, or nowhere.
std::optional<QVoice> voiceFor(QTextToSpeech& engine, QString const& accent)
{
    const QLocale::Territory territory = accent == QLatin1String("uk") ? QLocale::UnitedKingdom : QLocale::UnitedStates;
    for (const QVoice& voice : engine.availableVoices()) {
        const QLocale locale = voice.locale();
        if (locale.language() == QLocale::English and locale.territory() == territory)
            return voice;
    }
    return std::nullopt;
}

} // namespace

SpeechDuty::SpeechDuty(speech::PronunciationSource& source, speech::AudioCache& cache, StorageDuty& storage, QObject* parent)
    : QObject(parent),
      source_(source),
      cache_(cache),
      storage_(storage)
{
    localPool_.setMaxThreadCount(1);
    localVoiceIdle_.setSingleShot(true);
    localVoiceIdle_.setInterval(kLocalVoiceIdleMs);
    connect(&localVoiceIdle_, &QTimer::timeout, this, [this] {
        if (not localVoice_)
            return;
        LENS_INFO("pronunciation: the local voice has been idle for {} minutes; releasing its engine", kLocalVoiceIdleMs / 60000);
        retireLocalVoice();
    });
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
    // A queued recording posts back into this object, so the worker finishes before anything here goes.
    localPool_.waitForDone();
    disconnect(&downloader_, nullptr, this, nullptr);
    downloader_.cancel();
    // The player is a member and outlives nothing here: release() unbinds the buffer before the
    // player is destroyed, which is the one ordering this class has to get right.
    release();
}

void SpeechDuty::ensurePlayer()
{
    if (player_)
        return;
    output_ = std::make_unique<QAudioOutput>();
    player_ = std::make_unique<QMediaPlayer>();
    player_->setAudioOutput(output_.get());
    connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia or status == QMediaPlayer::InvalidMedia or status == QMediaPlayer::NoMedia)
            release();
    });
    connect(player_.get(), &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::StoppedState)
            setPlaying(false);
    });
    connect(player_.get(), &QMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error, QString const&) {
        // A recording that will not decode is a word that is not read: no notification, no retry.
        LENS_TRACE("pronunciation: the player refused a recording");
    });
}

QString SpeechDuty::accent() const
{
    return speech::accentFrom(rowValue(storage_, "speechAccent").toString());
}

QTextToSpeech* SpeechDuty::engineFor(QString const& name)
{
    if (const auto built = engines_.find(name); built != engines_.end())
        return built->second.get();

    auto        engine = std::make_unique<QTextToSpeech>(name);
    auto* const raw    = engine.get();
    engines_.emplace(name, std::move(engine));

    // The engine's own state is the only honest source for "the word is being read": a voice takes
    // as long as it takes, and a timer guessing at it would leave the icon lit after the sound.
    connect(raw, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
        if (state == QTextToSpeech::Speaking)
            setPlaying(true, systemWord_);
        else if (state == QTextToSpeech::Ready or state == QTextToSpeech::Error)
            setPlaying(false);
    });
    // A voice pack is enumerated by the engine's constructor, so this list is the whole answer and
    // it is not going to grow. It is logged once per engine per launch, and it is the only place
    // that says which voices this machine has: a reader reporting "no sound" is answered by it,
    // since a pack with no English voice for the accent is the commonest reason the default engine
    // cannot serve (ADR 0134).
    QStringList spoken;
    for (const QVoice& voice : raw->availableVoices())
        spoken.append(QStringLiteral("%1 (%2)").arg(voice.name(), voice.locale().name()));
    LENS_INFO("pronunciation: engine '{}', {} voice(s): {}", name.toStdString(), spoken.size(), spoken.join(QStringLiteral(", ")).toStdString());
    return raw;
}

QTextToSpeech* SpeechDuty::systemFor(QString const& accent)
{
    return systemVoiceFor(accent).engine;
}

SpeechDuty::SystemVoice const& SpeechDuty::systemVoiceFor(QString const& accent)
{
    if (auto const found = systemVoices_.find(accent); found != systemVoices_.end())
        return found->second;

    SystemVoice answer;
    for (QString const& name : systemEngines()) {
        auto* const engine = engineFor(name);
        if (engine == nullptr)
            continue;
        if (auto const voice = voiceFor(*engine, accent)) {
            answer = SystemVoice{engine, voice->name()};
            break;
        }
    }
    return systemVoices_.emplace(accent, answer).first->second;
}

bool SpeechDuty::systemAvailable()
{
    return systemFor(accent()) != nullptr;
}

QString SpeechDuty::systemVoice()
{
    return systemVoiceFor(accent()).name;
}

bool SpeechDuty::localAvailable() const
{
    const auto model = speech::voiceFrom(rowValue(storage_, "speechLocalEngine").toString());
    return speech::serves(model, accent()) and speech::installed(model, speech::voiceRoot());
}

void SpeechDuty::downloadVoice(QString id)
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

void SpeechDuty::removeVoice(QString id)
{
    const auto model = voiceNamed(id);
    if (not model or downloader_.busy())
        return;
    // The engine has to be released before its files are deleted, and the worker has to finish with it first,
    // so the next press loads the voice afresh if it is back.
    if (localVoice_ and localVoice_->model() == *model)
        retireLocalVoice();
    localPool_.waitForDone();
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
    release();
}

void SpeechDuty::release()
{
    if (releasing_)
        return;
    releasing_ = true;
    if (player_) {
        player_->stop();
        // Explicit, and before the buffer goes: the player holds a raw QIODevice, and a QBuffer
        // destroyed under it leaves the next read pointing at freed memory.
        player_->setSourceDevice(nullptr);
        player_->setSource(QUrl());
    }
    buffer_.reset();
    // The synthesiser is a second source of sound, so the same rule holds: a new word stops the one
    // being read, and nothing is left speaking over the next one.
    for (auto const& engine : engines_)
        engine.second->stop();
    setPlaying(false);
    releasing_ = false;
}

void SpeechDuty::speakSystem(QString const& word)
{
    auto* const engine = systemFor(accent());
    if (engine == nullptr)
        return;
    // The voice is picked per press rather than kept: the reader may have switched the accent
    // since the last word, and one engine can carry voices for both -- it holds whichever it was
    // last given.
    const auto voice = voiceFor(*engine, accent());
    if (not voice)
        return;
    if (engine->voice().name() != voice->name())
        engine->setVoice(*voice);
    LENS_TRACE("pronunciation: '{}' read by '{}'", word.toStdString(), voice->name().toStdString());
    systemWord_ = word;
    engine->say(word);
    // Lit at the press, not at the engine's first audio: the reader pressed a button and the icon is
    // its answer. stateChanged takes over from here, including a failure that stops it early.
    setPlaying(true, word);
}

void SpeechDuty::speak(QString word)
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

void SpeechDuty::retireLocalVoice()
{
    if (not localVoice_)
        return;
    // The capture is the point: the engine is destroyed when the worker drops the runnable, which it does after
    // whatever was queued for the engine before it. Destroying it here would stall the window for the teardown.
    localPool_.start(QRunnable::create([voice = std::move(localVoice_)] {}));
}

void SpeechDuty::speakLocal(QString const& word, QString const& accent)
{
    const auto model = speech::voiceFrom(rowValue(storage_, "speechLocalEngine").toString());
    // The engine is kept while the reader stays on one voice and one accent: loading it is the slow part.
    if (not localVoice_ or localVoice_->model() != model or localVoice_->accent() != accent) {
        retireLocalVoice();
        localVoice_ = std::make_shared<LocalVoice>(model, accent, speech::voiceRoot());
    }
    localVoiceIdle_.start();

    const int request = requestId_;
    localPool_.start(QRunnable::create([this, request, voice = localVoice_, word] {
        const std::optional<QByteArray> wav = voice->speak(word);
        if (not wav)
            return;
        // Posted back to this thread, where the player lives; a press replaced in the meantime is dropped there.
        QMetaObject::invokeMethod(this, [this, request, word, bytes = *wav] {
            if (request != requestId_)
                return;
            playBytes(word, bytes);
        });
    }));
}

void SpeechDuty::playOnline(QString const& word, QString const& accent)
{
    const QString key = speech::cacheKey(accent, word);
    if (key.isEmpty())
        return;

    if (auto cached = cache_.load(key)) {
        // A hit costs no request at all, which is what makes the second press on a word instant.
        playBytes(word, *cached);
        return;
    }

    const int request = requestId_;
    source_.fetch(word, accent, [this, request, key, word](QByteArray const& bytes, bool ok) {
        if (not ok or bytes.isEmpty())
            return;
        // Stored whatever the reader is looking at now: a recording is worth keeping even when the
        // card that asked for it has gone.
        cache_.store(key, bytes);
        emit cacheBytesChanged();
        if (request != requestId_)
            return;
        playBytes(word, bytes);
    });
}

void SpeechDuty::playBytes(QString const& word, QByteArray const& bytes)
{
    ensurePlayer();
    if (not player_ or bytes.isEmpty())
        return;

    // The new sound stops the old one before anything else, the first line of every player in the
    // survey: a reader pressing the speaker twice is the commonest thing this button sees. release()
    // is that stop, and it is also what leaves no buffer attached to the player.
    release();

    auto buffer = std::make_unique<QBuffer>();
    buffer->setData(bytes);
    if (not buffer->open(QIODevice::ReadOnly)) {
        LENS_TRACE("pronunciation: the recording could not be opened for reading");
        return;
    }
    buffer_ = std::move(buffer);
    player_->setSourceDevice(buffer_.get());
    player_->play();
    setPlaying(true, word);
}

}
