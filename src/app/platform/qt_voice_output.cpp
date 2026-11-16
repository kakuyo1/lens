/**
 * @file qt_voice_output.cpp
 * @brief The devices behind qt_voice_output.h, and the signals that say the sound ended.
 */

#include "app/platform/qt_voice_output.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QLocale>
#include <QMediaPlayer>
#include <QRunnable>
#include <QTextToSpeech>
#include <QVoice>

#include <optional>

#include "app/platform/local_voice.h"
#include "speech/voice_catalog.h"
#include "util/log.h"

namespace lens::app {
namespace {

/// How long the local engine stays loaded after the last press. Holding it costs about 390 MB and loading
/// it again about three seconds, so five minutes keeps a run of words instant without holding the memory
/// for the rest of the session.
constexpr int kLocalVoiceIdleMs = 5 * 60 * 1000;

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

QtVoiceOutput::QtVoiceOutput(QObject* parent)
    : QObject(parent)
{
    localPool_.setMaxThreadCount(1);
    localVoiceIdle_.setSingleShot(true);
    localVoiceIdle_.setInterval(kLocalVoiceIdleMs);
    connect(&localVoiceIdle_, &QTimer::timeout, this, [this] {
        if (not localVoice_)
            return;
        LENS_INFO("pronunciation: the local voice has been idle for {} minutes; releasing its engine", kLocalVoiceIdleMs / 60000); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
        retireLocalVoice();
    });
}

QtVoiceOutput::~QtVoiceOutput()
{
    // A queued recording posts back into this object, so the worker finishes before anything here goes.
    localPool_.waitForDone();
    // The player is a member and outlives nothing here: stop() unbinds the buffer before the
    // player is destroyed, which is the one ordering this class has to get right.
    stop();
}

void QtVoiceOutput::ensurePlayer()
{
    if (player_)
        return;
    output_ = std::make_unique<QAudioOutput>();
    player_ = std::make_unique<QMediaPlayer>();
    player_->setAudioOutput(output_.get());
    connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia and status != QMediaPlayer::InvalidMedia and status != QMediaPlayer::NoMedia)
            return;
        // stop() rebinds the source, which reports NoMedia again; the guard is inside it.
        if (releasing_)
            return;
        stop();
        notifySound(false, {});
    });
    connect(player_.get(), &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::StoppedState and not releasing_)
            notifySound(false, {});
    });
    connect(player_.get(), &QMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error, QString const&) {
        // A recording that will not decode is a word that is not read: no notification, no retry.
        LENS_TRACE("pronunciation: the player refused a recording"); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
    });
}

QString QtVoiceOutput::systemVoice(QString const& accent)
{
    return systemVoiceFor(accent).name;
}

bool QtVoiceOutput::installed(QString const& voiceId)
{
    return speech::installed(speech::voiceFrom(voiceId), speech::voiceRoot());
}

QTextToSpeech* QtVoiceOutput::engineFor(QString const& name)
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
            notifySound(true, systemWord_);
        else if (state == QTextToSpeech::Ready or state == QTextToSpeech::Error)
            notifySound(false, {});
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

QtVoiceOutput::SystemVoice const& QtVoiceOutput::systemVoiceFor(QString const& accent)
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

bool QtVoiceOutput::say(QString const& word, QString const& accent) // NOLINT(bugprone-easily-swappable-parameters): say takes its 2 const QString & arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    auto* const engine = systemVoiceFor(accent).engine;
    if (engine == nullptr)
        return false;
    // The voice is picked per press rather than kept: the reader may have switched the accent
    // since the last word, and one engine can carry voices for both -- it holds whichever it was
    // last given.
    const auto voice = voiceFor(*engine, accent);
    if (not voice)
        return false;
    if (engine->voice().name() != voice->name())
        engine->setVoice(*voice);
    LENS_TRACE("pronunciation: '{}' read by '{}'", word.toStdString(), voice->name().toStdString());
    systemWord_ = word;
    engine->say(word);
    return true;
}

bool QtVoiceOutput::play(QByteArray const& bytes)
{
    ensurePlayer();
    if (not player_ or bytes.isEmpty())
        return false;

    // The new sound stops the old one before anything else, the first line of every player in the
    // survey: a reader pressing the speaker twice is the commonest thing this button sees. stop()
    // is that stop, and it is also what leaves no buffer attached to the player.
    stop();

    auto buffer = std::make_unique<QBuffer>();
    buffer->setData(bytes);
    if (not buffer->open(QIODevice::ReadOnly)) {
        LENS_TRACE("pronunciation: the recording could not be opened for reading");
        return false;
    }
    buffer_ = std::move(buffer);
    player_->setSourceDevice(buffer_.get());
    player_->play();
    return true;
}

void QtVoiceOutput::speakLocal(QString const& word, QString const& accent, QString const& voiceId, LocalSink sink) // NOLINT(bugprone-easily-swappable-parameters): speakLocal takes its 3 const QString & arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    const auto model = speech::voiceFrom(voiceId);
    // The engine is kept while the reader stays on one voice and one accent: loading it is the slow part.
    if (not localVoice_ or localVoice_->model() != model or localVoice_->accent() != accent) {
        retireLocalVoice();
        localVoice_ = std::make_shared<LocalVoice>(model, accent, speech::voiceRoot());
    }
    localVoiceIdle_.start();

    localPool_.start(QRunnable::create([this, voice = localVoice_, word, sink] {
        const std::optional<QByteArray> wav = voice->speak(word);
        if (not wav) // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): QMetaObject::invokeMethod transfers the queued lambda into a QMetaCallEvent; the analyzer cannot see that ownership transfer
            return;
        // Posted back to the caller's thread; a press replaced in the meantime is dropped there.
        QMetaObject::invokeMethod(this, [sink, bytes = *wav] { // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): QMetaObject::invokeMethod transfers the queued lambda into a QMetaCallEvent; the analyzer cannot see that ownership transfer
            sink(bytes);
        });
    }));
}

void QtVoiceOutput::stop()
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
    releasing_ = false;
}

void QtVoiceOutput::retireLocalVoice()
{
    if (not localVoice_)
        return;
    // The capture is the point: the engine is destroyed when the worker drops the runnable, which it does after
    // whatever was queued for the engine before it. Destroying it here would stall the window for the teardown.
    localPool_.start(QRunnable::create([voice = std::move(localVoice_)] {}));
}

void QtVoiceOutput::releaseLocalVoice(QString const& voiceId)
{
    if (localVoice_ and speech::voiceSpec(localVoice_->model()).id == voiceId)
        retireLocalVoice();
    // The engine has to be out of the worker before its files are deleted, so the caller can
    // delete them when this returns.
    localPool_.waitForDone();
}

void QtVoiceOutput::notifySound(bool playing, QString const& word)
{
    if (onSoundState)
        onSoundState(playing, word);
}

}
