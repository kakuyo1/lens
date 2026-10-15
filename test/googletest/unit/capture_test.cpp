/** @file capture_test.cpp
 * @brief Offline capture policy, whitelist, stability and candidate boundaries.
 */
#include <gtest/gtest.h>

#include <climits>
#include <chrono>
#include <memory>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app/mouse_selection_hook.h"
#include "capture/capture_policy.h"
#include "capture/ocr_engine.h"
#include "capture/scan_pipeline.h"
#include "support.h"

namespace {

using namespace std::chrono_literals;
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

}

TEST(CapturePolicy, DragBoundaryHandlesBothDirections)
{
    EXPECT_FALSE(lens::capture::exceedsDragThreshold(-1, 0, 2));
    EXPECT_TRUE(lens::capture::exceedsDragThreshold(-2, 0, 2));
    EXPECT_TRUE(lens::capture::exceedsDragThreshold(0, 8, 8));
    EXPECT_TRUE(lens::app::isSelectionGesture({INT_MIN, 0, INT_MAX, 0}, 8));
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
