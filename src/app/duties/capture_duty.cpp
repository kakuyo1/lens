/**
 * @file capture_duty.cpp
 * @brief Selection capture and local OCR acquisition, independent of explanation presentation.
 */

#include "capture_duty.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <thread>

#include "capture/capture_policy.h"
#include "capture/foreground_window.h"
#include "core/domain/filter_core.h"
#include "core/domain/settings_table.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/settings_access.h"
#include "util/log.h"

namespace lens::app {
namespace {

/// @return The monitor behind a QScreen, in the same terms `capture::monitorAt` speaks.
///
/// A screen is looked up by the monitor it is rather than by the name it is called: Qt names a
/// screen by the monitor's friendly name and Win32 by its device name, and a comparison between
/// the two is always false -- which cost the screenshot path every capture and would have cost the
/// scanner every frame.
quintptr monitorOf(QScreen const* screen)
{
    if (auto* const native = screen->nativeInterface<QNativeInterface::QWindowsScreen>())
        return reinterpret_cast<quintptr>(native->handle());
    return 0;
}

constexpr int kMaxSelectionChars = 1000;

/// @return The Tesseract the reader pointed at, or the bundled runtime when both are empty.
capture::TesseractConfig storedTesseractConfig(StorageDuty& storage)
{
    auto const keys = storage.section();
    return {settings::read(keys, *core::settings::find("tesseractExecutable")).toString(),
            settings::read(keys, *core::settings::find("tesseractDataDirectory")).toString()};
}

/// @return The token for a reason, as the QString the QML boundary reads.
QString tokenOf(CaptureRefusal refusal)
{
    return QString::fromLatin1(refusalKey(refusal));
}

}

QString kindName(core::SelectionKind const kind)
{
    switch (kind) {
        case core::SelectionKind::Entity: return QStringLiteral("entity");
        case core::SelectionKind::Sentence: return QStringLiteral("sentence");
        case core::SelectionKind::Translation: return QStringLiteral("translation");
        case core::SelectionKind::Word: return QStringLiteral("word");
    }
    return QStringLiteral("word");
}

CaptureDuty::CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent)
    : CaptureDuty(storage, hook, capture::makeTesseractOcr(storedTesseractConfig(storage)), parent)
{}

CaptureDuty::CaptureDuty(StorageDuty&                        storage,
                         MouseSelectionHook&                 hook,
                         std::unique_ptr<capture::OcrEngine> ocr,
                         QObject*                            parent)
    : QObject(parent), storage_(storage), hook_(hook), grabber_(std::make_unique<SelectionTextGrabber>()),
      ocr_(std::move(ocr)), scan_(capture::makeScanPipeline())
{
    if (not ocr_) throw std::invalid_argument{"OCR backend must not be null"};
    connect(&hook_, &MouseSelectionHook::selectionReleased, this, &CaptureDuty::onSelectionReleased);
    connect(&hook_, &MouseSelectionHook::pointerPressed, this, &CaptureDuty::pointerPressed);

    auto const               names = settings().value("scanWhitelist").toString().split(';', Qt::SkipEmptyParts);
    std::vector<std::string> whitelist;
    for (auto const& name : names)
        whitelist.push_back(name.toStdString());
    scan_->setWhitelist(std::move(whitelist));
    scanTimer_.setInterval(1000);
    connect(&scanTimer_, &QTimer::timeout, this, &CaptureDuty::scanTick);
    scanTimer_.start();
    pollTimer_.setInterval(25);
    connect(&pollTimer_, &QTimer::timeout, this, &CaptureDuty::pollOcr);
    probeOcr();
}

CaptureDuty::~CaptureDuty()
{
    scanTimer_.stop();
    pollTimer_.stop();
    if (job_) job_->cancelled->store(true, std::memory_order_relaxed);
    job_.reset();
    jobContext_.reset();
}

QVariantMap CaptureDuty::settings() const
{
    auto const keys = storage_.section();
    // The six rows the settings table marks Signal::Capture, read through their own rules: a stored
    // length the document did not mean lands on the row's range the same way a written one would.
    const auto readOf = [&keys](std::string_view const key) {
        const auto* const row = core::settings::find(key);
        return settings::read(keys, *row);
    };
    return {{"ocrCapture", readOf("ocrCapture")},
            {"autoScan", readOf("autoScan")},
            {"ocrAvailable", ocrState_ == capture::OcrStatus::Ready},
            {"minimumWordLength", readOf("minimumWordLength")},
            {"tesseractExecutable", readOf("tesseractExecutable")},
            {"tesseractDataDirectory", readOf("tesseractDataDirectory")},
            // What an empty setting resolves to, for the surface that shows the reader where OCR
            // will look. Resolved through the engine's own function so the answer on screen cannot
            // drift from the path the process is actually started with.
            {"resolvedTesseractExecutable", capture::resolveTesseract(storedTesseractConfig(storage_)).executable},
            {"resolvedTesseractDataDirectory", capture::resolveTesseract(storedTesseractConfig(storage_)).dataDirectory},
            {"scanWhitelist", readOf("scanWhitelist")}};
}

QString CaptureDuty::ocrStatus() const
{
    if (jobContext_ and jobContext_->probe) return tokenOf(CaptureRefusal::Checking);
    // Ready is the status that is not a reason a capture cannot start, so it is the one string
    // here that has no CaptureRefusal behind it: the settings page reads it as "fine".
    const auto refusal = refusalOf(ocrState_);
    return refusal ? tokenOf(*refusal) : QStringLiteral("ready");
}

void CaptureDuty::invalidateScan()
{
    ++generation_;
    scanContext_.clear();
    scan_->reset();
}

bool CaptureDuty::setSetting(std::string_view const key, QVariant const& value)
{
    // One entry per row the settings table marks Signal::Capture, so the facade's generic write has
    // somewhere to land and the wiring below stays the duty's own business.
    if (key == "ocrCapture") {
        setOcrCapture(value.toBool());
        return true;
    }
    if (key == "autoScan") {
        setAutoScan(value.toBool());
        return true;
    }
    if (key == "minimumWordLength") {
        setMinimumWordLength(value.toInt());
        return true;
    }
    if (key == "scanWhitelist")
        return setScanWhitelist(value.toString());
    if (key == "tesseractExecutable") {
        setTesseractExecutable(value.toString());
        return true;
    }
    if (key == "tesseractDataDirectory") {
        setTesseractDataDirectory(value.toString());
        return true;
    }
    LENS_WARN("'{}' is not a capture setting; nothing was written", std::string{key});
    return false;
}

void CaptureDuty::setOcrCapture(bool const on)
{
    if (not settings::write(storage_.section(), *core::settings::find("ocrCapture"), on))
        return;
    ++screenshotGeneration_;
    emit settingsChanged();
}

void CaptureDuty::setAutoScan(bool const on)
{
    if (not settings::write(storage_.section(), *core::settings::find("autoScan"), on))
        return;
    invalidateScan();
    emit settingsChanged();
}

void CaptureDuty::setMinimumWordLength(int const length)
{
    if (not settings::write(storage_.section(), *core::settings::find("minimumWordLength"), length))
        return;
    invalidateScan();
    emit settingsChanged();
}

bool CaptureDuty::setScanWhitelist(QString processes)
{
    if (processes.size() > 4096) return false;
    auto const names = processes.split(QRegularExpression{QStringLiteral("[;,\\s]+")}, Qt::SkipEmptyParts);
    if (names.size() > 32) return false;
    static QRegularExpression const executable{QStringLiteral("^[A-Za-z0-9_.-]+\\.exe$"), QRegularExpression::CaseInsensitiveOption};
    QStringList                     normalized;
    std::vector<std::string>        whitelist;
    for (auto const& name : names) {
        if (name.size() > 128 or not executable.match(name).hasMatch()) return false;
        if (normalized.contains(name.toLower())) continue;
        normalized.append(name.toLower());
        whitelist.push_back(name.toLower().toStdString());
    }
    if (not settings::write(storage_.section(), *core::settings::find("scanWhitelist"), normalized.join(';')))
        return false;
    invalidateScan();
    scan_->setWhitelist(std::move(whitelist));
    emit settingsChanged();
    return true;
}

void CaptureDuty::setTesseractExecutable(QString executable)
{
    const QString value = executable.trimmed();
    auto          keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("tesseractExecutable")).toString()) return;
    settings::write(keys, *core::settings::find("tesseractExecutable"), value);
    rebuildOcr();
}

void CaptureDuty::setTesseractDataDirectory(QString directory)
{
    const QString value = directory.trimmed();
    auto          keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("tesseractDataDirectory")).toString()) return;
    settings::write(keys, *core::settings::find("tesseractDataDirectory"), value);
    rebuildOcr();
}

void CaptureDuty::rebuildOcr()
{
    ocr_ = capture::makeTesseractOcr(storedTesseractConfig(storage_));
    // A probe in flight belongs to the engine that just went away, and its answer would land on
    // the new one's state. Drop it the way the destructor does, then ask again.
    if (job_) {
        job_->cancelled->store(true, std::memory_order_relaxed);
        job_.reset();
        jobContext_.reset();
        pollTimer_.stop();
    }
    probeOcr();
    emit settingsChanged();
}

void CaptureDuty::restoreDefaults()
{
    // The rows the table marks Signal::Capture, in the table's order, through the same write path a
    // choice takes -- so a restore and a fresh install read one list rather than two.
    for (auto const& row : core::settings::kRows)
        if (row.signal == core::settings::Signal::Capture and row.restorable)
            setSetting(row.key, settings::defaultValue(row));
}

void CaptureDuty::setScanPaused(bool const paused)
{
    if (scanPaused_ == paused) return;
    scanPaused_ = paused;
    invalidateScan();
}

void CaptureDuty::setBudgetPaused(bool const paused)
{
    if (budgetPaused_ == paused) return;
    budgetPaused_ = paused;
    ++screenshotGeneration_;
    invalidateScan();
}

void CaptureDuty::probeOcr()
{
    if (job_) return;
    jobContext_ = OcrJob{.probe = true};
    auto state  = std::make_shared<OcrJobState>();
    job_        = state;
    auto engine = ocr_;
    std::thread([state, engine = std::move(engine)] {
        capture::OcrResult result;
        try {
            result.status = engine->probe(state->cancelled);
        } catch (...) {
            result.status = capture::OcrStatus::Failed;
        }
        std::lock_guard lock{state->mutex};
        state->result = std::move(result);
        state->done   = true;
    }).detach();
    pollTimer_.start();
    emit settingsChanged();
}

void CaptureDuty::launchOcr(QImage image, QPoint const anchor, QString context)
{
    auto const generation = context.isEmpty() ? screenshotGeneration_ : generation_;
    jobContext_           = OcrJob{.anchor = anchor, .context = std::move(context), .generation = generation};
    auto state            = std::make_shared<OcrJobState>();
    job_                  = state;
    auto engine           = ocr_;
    std::thread([state, engine = std::move(engine), image = std::move(image)] {
        capture::OcrResult result;
        try {
            result = engine->recognize(image, state->cancelled);
        } catch (...) {
            result.status = capture::OcrStatus::Failed;
        }
        std::lock_guard lock{state->mutex};
        state->result = std::move(result);
        state->done   = true;
    }).detach();
    pollTimer_.start();
}

std::optional<CaptureRefusal> CaptureDuty::captureRefusalReason() const
{
    return gateRefusal({.budgetPaused = budgetPaused_,
                        .probing      = jobContext_ and jobContext_->probe,
                        .busy         = job_ != nullptr,
                        .screenshotOn = settings().value("ocrCapture").toBool(),
                        .runtime      = ocrState_});
}

bool CaptureDuty::captureScreenshot(QImage image, QPoint const anchor)
{
    if (captureRefusalReason()) return false;
    launchOcr(std::move(image), anchor, {});
    return true;
}

std::optional<CaptureRefusal> CaptureDuty::captureRegion(QRect const region)
{
    if (const auto refusal = captureRefusalReason()) return refusal;
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) return CaptureRefusal::OutsideScreen;
    if (region.width() * screen->devicePixelRatio() > 8192 or region.height() * screen->devicePixelRatio() > 8192) return CaptureRefusal::RegionTooLarge;
    auto const nativeOrigin = capture::physicalScreenOrigin(monitorOf(screen));
    if (not nativeOrigin) return CaptureRefusal::OutsideScreen;
    auto const local  = region.translated(-screen->geometry().topLeft());
    auto const image  = screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
    auto const anchor = *nativeOrigin + local.topLeft() * screen->devicePixelRatio();
    // The gate was read a moment ago; a job that started since is the only way this refuses now,
    // and it is the same answer the next press will get.
    return captureScreenshot(image, anchor) ? std::nullopt : std::optional<CaptureRefusal>{CaptureRefusal::Busy};
}

void CaptureDuty::scanTick()
{
    if (budgetPaused_ or scanPaused_ or not settings().value("autoScan").toBool() or ocrState_ != capture::OcrStatus::Ready) return;
    auto const foreground = capture::foregroundWindow();
    if (not foreground) {
        invalidateScan();
        return;
    }
    capture::ScanFrame frame{.context     = foreground->context.toStdString(),
                             .processName = foreground->processName.toStdString(),
                             .ownProcess  = foreground->ownProcess};
    if (not scan_->allows(frame)) {
        invalidateScan();
        return;
    }
    if (scanContext_ != foreground->context) {
        invalidateScan();
        scanContext_ = foreground->context;
    }
    if (job_) return;
    QScreen* screen = nullptr;
    for (auto* const candidate : QGuiApplication::screens())
        if (monitorOf(candidate) == foreground->monitor) {
            screen = candidate;
            break;
        }
    if (screen == nullptr) return;
    auto const image = screen->grabWindow(foreground->window).toImage();
    if (image.isNull()) {
        invalidateScan();
        return;
    }
    frame.pixels   = capture::pixelFingerprint(image);
    auto const now = std::chrono::steady_clock::now();
    scanAnchor_    = foreground->anchor;
    if (scan_->needsOcr(frame, now))
        launchOcr(image, scanAnchor_, scanContext_);
    else
        publishBatch(now, scanAnchor_);
}

void CaptureDuty::pollOcr()
{
    if (not job_) return;
    auto               state = job_;
    capture::OcrResult result;
    {
        std::lock_guard lock{state->mutex};
        if (not state->done) return;
        result = state->result;
    }
    auto const context = *jobContext_;
    job_.reset();
    jobContext_.reset();
    pollTimer_.stop();
    if (context.probe) {
        ocrState_ = result.status;
        emit settingsChanged();
        if (result.status != capture::OcrStatus::Ready)
            LENS_WARN("OCR probe failed (status {})", ocrStatus().toStdString());
        return;
    }
    std::optional<QString> foregroundContext;
    if (not context.context.isEmpty()) {
        if (auto const foreground = capture::foregroundWindow()) foregroundContext = foreground->context;
    }
    auto const fate = capture::ocrResultFate({.jobContext           = context.context,
                                              .jobGeneration        = context.generation,
                                              .scanGeneration       = generation_,
                                              .screenshotGeneration = screenshotGeneration_,
                                              .foregroundContext    = std::move(foregroundContext),
                                              .budgetPaused         = budgetPaused_,
                                              .scanPaused           = scanPaused_,
                                              .autoScan             = settings().value("autoScan").toBool()});
    if (fate == capture::OcrResultFate::Superseded) return;
    if (fate == capture::OcrResultFate::Withdrawn) {
        invalidateScan();
        return;
    }
    if (result.status != capture::OcrStatus::Ready) {
        // The status this result carried, never ocrStatus(): that reads ocrState_, which the gate
        // requires to be Ready before a framed capture starts at all -- so ocrStatus() would report
        // the runtime as fine about a region that simply had no text in it.
        const auto refusal = refusalOf(result.status).value_or(CaptureRefusal::Failed);
        if (context.context.isEmpty()) {
            LENS_INFO("OCR of the framed region produced no text (status {})", refusalKey(refusal));
            emit screenshotFailed(refusal);
            return;
        }
        scan_->reset();
        if (capture::isRuntimeFault(result.status)) {
            ocrState_ = result.status;
            emit settingsChanged();
            LENS_WARN("OCR worker failed (status {})", ocrStatus().toStdString());
        }
        return;
    }
    if (context.context.isEmpty()) {
        // Only the screenshot path reaches here with no scan context: a selection brings its own
        // text rather than going through OCR at all.
        classifyText(result.text, context.anchor, true);
        return;
    }
    auto const unsupported = capture::unsupportedEntityCount(result.text.toStdString());
    if (unsupported > 0) {
        LENS_INFO("automatic scan retained {} unsupported entity candidate(s); entity channel is not enabled", unsupported);
        emit scanEntitiesUnsupported(static_cast<int>(unsupported));
    }
    auto const now = std::chrono::steady_clock::now();
    scan_->acceptText(result.text.toStdString(),
                      {.minimumLength = settings().value("minimumWordLength").toInt(),
                       .knownLemmas   = storage_.knownStore().known(),
                       .minimumRank   = minFreqRank()},
                      now);
    publishBatch(now, context.anchor);
}

void CaptureDuty::publishBatch(capture::ScanTime const now, QPoint const anchor)
{
    const auto   batch = scan_->takeBatch(now);
    QVariantList words;
    for (auto const& word : batch)
        words.append(QVariantMap{{"surface", QString::fromStdString(word.surface)},
                                 {"lemma", QString::fromStdString(word.lemma)}});
    if (not words.isEmpty()) emit scanCandidatesReady(words, anchor);
}

void CaptureDuty::onSelectionReleased(QPoint const anchor)
{
    if (budgetPaused_) return;
    if (not settings::read(storage_.section(), *core::settings::find("selectionCapture")).toBool()) {
        LENS_TRACE("selection at ({}, {}) ignored: selection capture is off", anchor.x(), anchor.y());
        return;
    }
    beginSelection(anchor);
}

void CaptureDuty::beginSelection(QPoint const anchor, std::optional<QString> const text)
{
    if (text) {
        classifyText(*text, anchor);
        return;
    }
    const auto policy = clipboardPolicyFromStored(
        settings::read(storage_.section(), *core::settings::find("clipboardPolicy")).toString().toStdString());
    const auto grabbed = grabber_->grab(policy);
    if (const auto* status = std::get_if<GrabStatus>(&grabbed)) {
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return;
    }
    classifyText(std::get<QString>(grabbed), anchor);
}

void CaptureDuty::classifyText(QString text, QPoint const anchor, bool const fromScreenshot)
{
    if (text.size() > kMaxSelectionChars) text = text.left(kMaxSelectionChars) + QStringLiteral("…");
    const core::Selection selection = core::classifySelection(text.toStdString(), storage_.knownStore().known(), minFreqRank());
    PendingSelection      pending{.anchor = anchor, .text = text, .kind = selection.kind};
    if (selection.kind == core::SelectionKind::Word) {
        // Only a word narrows to a candidate: the other kinds have no lemma to pick, and the whole
        // selection is what gets answered.
        const auto& candidates = selection.candidates;
        const auto  fresh      = std::find_if(candidates.begin(), candidates.end(), [](const core::Candidate& candidate) {
            return candidate.state == core::CandidateState::New;
        });
        if (candidates.empty()) return;
        const core::Candidate& word = fresh != candidates.end() ? *fresh : candidates.front();
        pending.surface             = QString::fromStdString(word.surface);
        pending.lemma               = QString::fromStdString(word.lemma);
    }
    emit selectionReady(pending);
    if (fromScreenshot) {
        // The reader framed this region themselves, so they have already said what they want done
        // with it: translate. The bar would ask again -- and OCR exists for text that cannot be
        // selected, which is exactly the text whose reader wants the meaning rather than a menu.
        emit selectionActionRequested(QStringLiteral("translate"), pending.text);
        return;
    }
    emit selectionBarRequested(QVariantMap{{"x", anchor.x()},
                                           {"y", anchor.y()},
                                           {"kind", kindName(pending.kind)},
                                           {"text", pending.text},
                                           {"letterCount", capture::selectionLetterCount(pending.text)}});
}

std::size_t CaptureDuty::minFreqRank() const
{
    return core::rankForLevel(storage_.knownStore().level());
}

}
