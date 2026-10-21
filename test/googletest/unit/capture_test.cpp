/** @file capture_test.cpp
 * @brief Offline capture policy, whitelist, stability, candidate boundaries and the OCR seam.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <climits>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include "app/duties/capture_duty.h"
#include "app/duties/capture_refusal.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/duties/storage_duty.h"
#include "capture/capture_policy.h"
#include "capture/ocr_engine.h"
#include "capture/scan_pipeline.h"
#include "core/document/known_store.h"
#include "support.h"

namespace {

using namespace std::chrono_literals;
using lens::app::ClipboardBorrow;
using lens::app::ClipboardPolicy;
using lens::app::ClipboardSnapshot;
using lens::app::Gesture;
using lens::app::isSelectionGesture;
using lens::app::selectionSurvivesClipboardPolicy;
using lens::capture::FilterPolicy;
using lens::capture::ScanFrame;
using lens::capture::ScanPipeline;
using lens::capture::ScanTime;
using lens::test::CoreTest;

ScanTime at(int seconds)
{
    return ScanTime{} + std::chrono::seconds{seconds};
}

ScanFrame frame(std::uint64_t pixels = 1, std::string context = "window")
{
    return {.context = std::move(context), .processName = "chrome.exe", .pixels = pixels};
}

std::unique_ptr<ScanPipeline> scanner()
{
    auto scan = lens::capture::makeScanPipeline();
    scan->setWhitelist({"chrome.exe"});
    return scan;
}

void observe(ScanPipeline& scan, int seconds, std::string_view text, std::uint64_t pixels = 1, FilterPolicy policy = {})
{
    if (scan.needsOcr(frame(pixels), at(seconds))) scan.acceptText(text, policy, at(seconds));
}

/// @brief Put a `<code>.traineddata` of @p bytes into @p directory; zero is the truncated file a
///        half-finished download leaves behind.
/// @return Whether the file could be written, so the caller's assertion is about the rule and not
///         about the temporary directory.
bool writeTrainedData(QString const& directory, const char* code, qint64 bytes = 1)
{
    QFile file{QDir{directory}.filePath(QString::fromLatin1(code) + QStringLiteral(".traineddata"))};
    if (not file.open(QIODevice::WriteOnly)) return false;
    return file.write(QByteArray(static_cast<int>(bytes), 'x')) == bytes;
}

/// @brief The in-memory OcrEngine: answers what it is scripted to, the way TesseractOcr frames it.
///
/// A null image is InvalidImage and a raised cancel flag is Failed before any recognition, which
/// is the contract the production adapter keeps; everything else is the scripted result.
struct FakeOcr final : lens::capture::OcrEngine {
    lens::capture::OcrResult result{.status = lens::capture::OcrStatus::EmptyText};

    lens::capture::OcrStatus probe(std::shared_ptr<std::atomic_bool> const cancel) const override
    {
        if (cancel and cancel->load(std::memory_order_relaxed)) return lens::capture::OcrStatus::Failed;
        return lens::capture::OcrStatus::Ready;
    }

    lens::capture::OcrResult recognize(QImage const& image, std::shared_ptr<std::atomic_bool> const cancel) const override
    {
        if (image.isNull()) return {.status = lens::capture::OcrStatus::InvalidImage};
        if (cancel and cancel->load(std::memory_order_relaxed)) return {.status = lens::capture::OcrStatus::Failed};
        return result;
    }
};

/// @brief The in-memory ClipboardPort: scripted sequence and text, recording restores.
struct FakeClipboard final : lens::app::ClipboardPort {
    std::optional<ClipboardSnapshot> snapshot       = ClipboardSnapshot{};
    std::optional<QString>           text           = QStringLiteral("word");
    unsigned long                    sequenceNumber = 10;
    bool                             restored       = false;

    std::optional<ClipboardSnapshot> take() override
    {
        return snapshot;
    }

    bool restore(ClipboardSnapshot const&) override
    {
        restored = true;
        return true;
    }

    unsigned long sequence() override
    {
        return sequenceNumber;
    }

    bool waitForChange(unsigned long) override
    {
        return true;
    }

    std::optional<QString> readText() override
    {
        return text;
    }
};

/// @brief A scan result whose generation and window both still match, with scanning on.
lens::capture::OcrResultContext currentScan()
{
    return {.jobContext        = QStringLiteral("window"),
            .jobGeneration     = 4,
            .scanGeneration    = 4,
            .foregroundContext = QStringLiteral("window"),
            .autoScan          = true};
}

/// @brief Run the event loop until @p signal leaves @p sender, or until the bound passes.
///
/// The bound only keeps a broken wiring from hanging the suite. This target does not link Qt Test,
/// so there is no QSignalSpy; a plain connection and a single-shot timer do the same job.
/// @return Whether the signal came before the bound.
template <typename Sender, typename Signal>
bool awaitSignal(Sender const& sender, Signal signal)
{
    QEventLoop loop;
    QTimer     bound;
    bound.setSingleShot(true);
    bool fired = false;
    QObject::connect(&sender, signal, &loop, [&fired, &loop] {
        fired = true;
        loop.quit();
    });
    QObject::connect(&bound, &QTimer::timeout, &loop, &QEventLoop::quit);
    bound.start(2000);
    loop.exec();
    return fired;
}

}

TEST(CapturePolicy, DragBoundaryHandlesBothDirections)
{
    EXPECT_FALSE(lens::capture::exceedsDragThreshold(-1, 0, 2));
    EXPECT_TRUE(lens::capture::exceedsDragThreshold(-2, 0, 2));
    EXPECT_TRUE(lens::capture::exceedsDragThreshold(0, 8, 8));
    EXPECT_TRUE(lens::app::isSelectionGesture({INT_MIN, 0, INT_MAX, 0}, 8));
}

/// Passed explicitly rather than read from the system, so the cases do not depend on the mouse
/// settings of whichever machine runs them.
constexpr int kSlop = 4;

TEST(SelectionGesture, JitterInsideTheSlopIsStillAClick)
{
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100 + kSlop - 1, 200}, kSlop));
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100, 200 + kSlop - 1}, kSlop));
}

TEST(SelectionGesture, MovementAtTheSlopCounts)
{
    // The boundary is inclusive: exactly the slop is already a drag, which is the reading
    // that keeps a two-character selection from being swallowed.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100 + kSlop, 200}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200 + kSlop}, kSlop));

    // Dragging up or to the left selects just as much as dragging down or right.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100 - kSlop, 200}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200 - kSlop}, kSlop));
}

TEST(SelectionGesture, AStationaryClickNeverSelects)
{
    // Double-clicking and triple-clicking have the same stationary endpoints here. Only a drag
    // should open Lens's selection action bar, so click runs remain outside the selection path.
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100, 200}, kSlop));
}

TEST(ClipboardPolicy, SilentDropsOnlyAReplacedClipboard)
{
    EXPECT_TRUE(selectionSurvivesClipboardPolicy(ClipboardPolicy::Topmost, false));
    EXPECT_TRUE(selectionSurvivesClipboardPolicy(ClipboardPolicy::Topmost, true));
    EXPECT_TRUE(selectionSurvivesClipboardPolicy(ClipboardPolicy::Silent, false));
    EXPECT_FALSE(selectionSurvivesClipboardPolicy(ClipboardPolicy::Silent, true));
}

TEST(ClipboardBorrow, RefusesToBorrowWhatItCannotTake)
{
    FakeClipboard clipboard;
    clipboard.snapshot = std::nullopt;

    ClipboardBorrow borrow{clipboard};
    EXPECT_FALSE(borrow.begin());
    EXPECT_FALSE(clipboard.restored);
}

TEST(ClipboardBorrow, LeavesARewrittenClipboardAlone)
{
    FakeClipboard   clipboard;
    ClipboardBorrow borrow{clipboard};
    ASSERT_TRUE(borrow.begin());

    clipboard.sequenceNumber = 11; // our copy landed
    borrow.markCopySequence();
    clipboard.sequenceNumber = 12; // another process wrote after it

    bool replaced = false;
    EXPECT_TRUE(borrow.settle(true, replaced));
    EXPECT_TRUE(replaced);
    EXPECT_FALSE(clipboard.restored) << "a snapshot put back over a newer clipboard destroys it";
}

TEST(ClipboardBorrow, PutsTheSnapshotBackWhenNothingElseWrote)
{
    FakeClipboard   clipboard;
    ClipboardBorrow borrow{clipboard};
    ASSERT_TRUE(borrow.begin());

    clipboard.sequenceNumber = 11;
    borrow.markCopySequence();

    bool replaced = true;
    EXPECT_TRUE(borrow.settle(true, replaced));
    EXPECT_FALSE(replaced);
    EXPECT_TRUE(clipboard.restored);
}

TEST(ClipboardBorrow, PutsTheSnapshotBackWhenTheCopyNeverLanded)
{
    FakeClipboard   clipboard;
    ClipboardBorrow borrow{clipboard};
    ASSERT_TRUE(borrow.begin());

    bool replaced = true;
    EXPECT_TRUE(borrow.settle(false, replaced));
    EXPECT_FALSE(replaced);
    EXPECT_TRUE(clipboard.restored);
}

TEST(CapturePolicy, ExplicitSelectionLengthCountsUnicodeLettersOnly)
{
    EXPECT_EQ(lens::capture::selectionLetterCount(QStringLiteral("ab3!")), 2);
    EXPECT_EQ(lens::capture::selectionLetterCount(QStringLiteral("word")), 4);
    EXPECT_EQ(lens::capture::selectionLetterCount(QString::fromUtf8("é中")), 2);
}

TEST(CapturePolicy, TheOcrLanguagesFollowWhatTheDataDirectoryHolds)
{
    // The reader supplies the tessdata directory, so whether Chinese can be read is a property of
    // that directory and of nothing else. A run that names a language it has no data for fails
    // outright, which is why the list is read off the files rather than assumed.
    QTemporaryDir english;
    ASSERT_TRUE(english.isValid());
    QTemporaryDir both;
    ASSERT_TRUE(both.isValid());
    QTemporaryDir chineseOnly;
    ASSERT_TRUE(chineseOnly.isValid());
    QTemporaryDir truncated;
    ASSERT_TRUE(truncated.isValid());

    EXPECT_TRUE(lens::capture::tesseractLanguages(english.path()).isEmpty()) << "an untrained directory reads nothing";

    ASSERT_TRUE(writeTrainedData(english.path(), "eng"));
    EXPECT_EQ(lens::capture::tesseractLanguages(english.path()), QStringLiteral("eng"));

    // Chinese first of the pair: a reader who installed it did so to read Chinese.
    ASSERT_TRUE(writeTrainedData(both.path(), "eng"));
    ASSERT_TRUE(writeTrainedData(both.path(), "chi_sim"));
    EXPECT_EQ(lens::capture::tesseractLanguages(both.path()), QStringLiteral("chi_sim+eng"));

    // The pair is not required: Chinese alone is a working directory, and so is English alone.
    ASSERT_TRUE(writeTrainedData(chineseOnly.path(), "chi_sim"));
    EXPECT_EQ(lens::capture::tesseractLanguages(chineseOnly.path()), QStringLiteral("chi_sim"));

    // Zero bytes is what a half-finished download leaves behind, and naming it fails the run.
    ASSERT_TRUE(writeTrainedData(truncated.path(), "eng", 0));
    EXPECT_TRUE(lens::capture::tesseractLanguages(truncated.path()).isEmpty());
}

TEST_F(CoreTest, ScanRequiresWhitelistStableFramesAndTwoSecondRateLimit)
{
    auto scan = scanner();
    observe(*scan, 0, "resilience ubiquitous");
    observe(*scan, 1, "resilience ubiquitous");
    EXPECT_EQ(scan->takeBatch(at(1)).size(), 0u);
    observe(*scan, 2, "resilience ubiquitous");
    EXPECT_EQ(scan->takeBatch(at(2)).size(), 2u);
    observe(*scan, 3, "resilience ambiguous", 2);
    observe(*scan, 4, "resilience ambiguous", 2);
    observe(*scan, 5, "resilience ambiguous", 2);
    EXPECT_EQ(scan->takeBatch(at(5)).size(), 2u);
}

TEST_F(CoreTest, ContinuousMinimumLengthDoesNotLeakExplicitTokenExemption)
{
    EXPECT_EQ(lens::capture::filterProse("am cat book", {.minimumLength = 2}).size(), 3u);
    EXPECT_EQ(lens::capture::filterProse("am cat book", {.minimumLength = 5}).size(), 0u);
    EXPECT_TRUE(lens::capture::filterProse("QML Kubernetes MP3", {.minimumLength = 2}).empty());
    EXPECT_EQ(lens::capture::unsupportedEntityCount("I visited New York and QML API"), 2u);
}

TEST(CapturePolicy, ProcessGateIsExactAndFailClosed)
{
    EXPECT_TRUE(lens::capture::allowedProcess("Apps/CHROME.EXE", {"chrome.exe"}, false));
    EXPECT_FALSE(lens::capture::allowedProcess("not-chrome.exe", {"chrome.exe"}, false));
    EXPECT_FALSE(lens::capture::allowedProcess("chrome.exe", {"chrome.exe"}, true));
    EXPECT_FALSE(lens::capture::allowedProcess("", {"chrome.exe"}, false));
}

TEST(CapturePolicy, AStaleScreenshotResultIsDroppedAndACurrentOneApplies)
{
    using lens::capture::OcrResultFate;
    lens::capture::OcrResultContext screenshot{.jobGeneration = 2, .scanGeneration = 9, .screenshotGeneration = 2};
    EXPECT_EQ(lens::capture::ocrResultFate(screenshot), OcrResultFate::Current) << "only the screenshot generation counts";
    // A screenshot does not care about the window or the scan switch.
    screenshot.scanPaused   = true;
    screenshot.budgetPaused = true;
    EXPECT_EQ(lens::capture::ocrResultFate(screenshot), OcrResultFate::Current);
    screenshot.screenshotGeneration = 3;
    EXPECT_EQ(lens::capture::ocrResultFate(screenshot), OcrResultFate::Superseded);
}

TEST(CapturePolicy, AStaleScanResultIsWithdrawnAndAMovedGenerationOutranksIt)
{
    using lens::capture::OcrResultFate;
    EXPECT_EQ(lens::capture::ocrResultFate(currentScan()), OcrResultFate::Current);

    auto moved           = currentScan();
    moved.scanGeneration = 5;
    EXPECT_EQ(lens::capture::ocrResultFate(moved), OcrResultFate::Superseded);
    moved.scanPaused = true;
    EXPECT_EQ(lens::capture::ocrResultFate(moved), OcrResultFate::Superseded) << "the generation is read first";

    auto budget         = currentScan();
    budget.budgetPaused = true;
    EXPECT_EQ(lens::capture::ocrResultFate(budget), OcrResultFate::Withdrawn);
    auto paused       = currentScan();
    paused.scanPaused = true;
    EXPECT_EQ(lens::capture::ocrResultFate(paused), OcrResultFate::Withdrawn);
    auto noWindow              = currentScan();
    noWindow.foregroundContext = std::nullopt;
    EXPECT_EQ(lens::capture::ocrResultFate(noWindow), OcrResultFate::Withdrawn);
    auto otherWindow              = currentScan();
    otherWindow.foregroundContext = QStringLiteral("other");
    EXPECT_EQ(lens::capture::ocrResultFate(otherWindow), OcrResultFate::Withdrawn);
    auto switchedOff     = currentScan();
    switchedOff.autoScan = false;
    EXPECT_EQ(lens::capture::ocrResultFate(switchedOff), OcrResultFate::Withdrawn);
}

TEST(CapturePolicy, AnEmptyOrUnreadableRegionIsNotARuntimeFault)
{
    using lens::capture::OcrStatus;
    EXPECT_FALSE(lens::capture::isRuntimeFault(OcrStatus::Ready));
    EXPECT_FALSE(lens::capture::isRuntimeFault(OcrStatus::EmptyText));
    EXPECT_FALSE(lens::capture::isRuntimeFault(OcrStatus::InvalidImage));
    EXPECT_TRUE(lens::capture::isRuntimeFault(OcrStatus::RuntimeMissing));
    EXPECT_TRUE(lens::capture::isRuntimeFault(OcrStatus::LanguageMissing));
    EXPECT_TRUE(lens::capture::isRuntimeFault(OcrStatus::StartFailed));
    EXPECT_TRUE(lens::capture::isRuntimeFault(OcrStatus::TimedOut));
    EXPECT_TRUE(lens::capture::isRuntimeFault(OcrStatus::Failed));
}

/// A framed region that reads no text is reported with the status its own result carried, not with
/// the runtime's: the runtime is Ready by the time a capture can start, so ocrStatus() would say
/// "ready" about a region that had nothing in it.
TEST(CaptureDuty, AFramedRegionThatReadsNoTextReportsTheResultsOwnReason)
{
    using lens::app::CaptureDuty;
    using lens::app::CaptureRefusal;
    using lens::app::MouseSelectionHook;
    using lens::app::StorageDuty;

    int              argc   = 1;
    char             name[] = "capture_test";
    char*            argv[] = {name, nullptr};
    QCoreApplication application{argc, argv}; // the probe and the screenshot's result arrive on the event loop

    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto        store = lens::core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    StorageDuty storage{store};
    storage.section().set<bool>("ocrCapture", true);

    // Constructed but never install()ed: only main() installs the hook.
    MouseSelectionHook            hook;
    std::optional<CaptureRefusal> reported;
    CaptureDuty                   duty{storage, hook, std::make_unique<FakeOcr>()};
    QObject::connect(&duty, &CaptureDuty::screenshotFailed, [&reported](CaptureRefusal refusal) { reported = refusal; });

    // The gate stays shut until the probe has answered on the event loop.
    ASSERT_TRUE(awaitSignal(duty, &CaptureDuty::settingsChanged));
    ASSERT_EQ(duty.captureRefusalReason(), std::nullopt);

    ASSERT_TRUE(duty.captureScreenshot(QImage{4, 4, QImage::Format_Grayscale8}, {}));
    ASSERT_TRUE(awaitSignal(duty, &CaptureDuty::screenshotFailed));
    EXPECT_EQ(reported, CaptureRefusal::EmptyText);
}
