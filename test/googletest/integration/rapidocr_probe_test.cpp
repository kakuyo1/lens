/**
 * @file rapidocr_probe_test.cpp
 * @brief The real RapidOCR-json binary, against the committed synthetic corpus.
 *
 * The distribution is user-installed and never committed; the test skips unless LENS_RAPIDOCR_ROOT
 * names an extracted RapidOCR-json_v0.2.0 folder. It reads no desktop pixels and makes no network
 * request. Human-run:
 * `powershell -File scripts/qa/gtest.ps1 -Target lens_gtest_integration -Filter 'RapidOcrProbe*'`
 * with LENS_RAPIDOCR_ROOT set.
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include <QDir>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include "capture/ocr_rapidocr.h"

namespace {

using lens::capture::OcrGeometryLevel;
using lens::capture::OcrLanguage;
using lens::capture::OcrRequest;
using lens::capture::OcrSource;
using lens::capture::OcrStatus;
using lens::capture::RapidOcrConfig;

std::filesystem::path corpus()
{
    return std::filesystem::path{LENS_SOURCE_DIR} / "test" / "ocr" / "corpus";
}

QString envPath(char const* name)
{
    char const* value = std::getenv(name);
    return value == nullptr ? QString{} : QString::fromLocal8Bit(value);
}

std::unique_ptr<lens::capture::OcrEngine> engineFor(OcrLanguage const language)
{
    auto const root = envPath("LENS_RAPIDOCR_ROOT");
    if (root.isEmpty()) return {};
    RapidOcrConfig config{.executable      = QDir{root}.filePath(QStringLiteral("RapidOCR-json.exe")),
                          .modelsDirectory = QDir{root}.filePath(QStringLiteral("models")),
                          .language        = language};
    config.trustDigest = lens::capture::rapidOcrTrustDigest(config);
    if (config.trustDigest.isEmpty()) return {};
    return lens::capture::makeRapidOcr(config);
}

TEST(RapidOcrProbe, ReadsASyntheticEnglishPageWithLineGeometryAndNoWordBoxes)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_RAPIDOCR_ROOT is not set or the distribution is incomplete";

    QImage image{QString::fromStdWString((corpus() / "browser-en-100-light.png").wstring())};
    ASSERT_FALSE(image.isNull());
    auto const result = engine->recognize(
        {.requestId = 11, .source = OcrSource::LocalTest, .image = image, .language = OcrLanguage::English});

    ASSERT_EQ(result.status, OcrStatus::Ready) << "status " << static_cast<int>(result.status);
    EXPECT_EQ(result.requestId, 11u);
    EXPECT_EQ(result.actualEngine, QStringLiteral("rapidocr-json"));
    EXPECT_EQ(result.languageUsed, QStringLiteral("en"));
    EXPECT_TRUE(result.text.contains(QStringLiteral("harbour"), Qt::CaseInsensitive));
    EXPECT_EQ(result.geometryLevel, OcrGeometryLevel::Line);
    EXPECT_FALSE(result.confidence);
    EXPECT_FALSE(result.lines.empty());
    for (auto const& line : result.lines) {
        ASSERT_TRUE(line.box);
        EXPECT_TRUE(lens::capture::boxFitsImage(*line.box, image.size()));
        EXPECT_TRUE(line.words.empty());
    }
}

TEST(RapidOcrProbe, ABarePageIsEmptyTextAndCancellationNeverStartsTheProgram)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_RAPIDOCR_ROOT is not set or the distribution is incomplete";

    QImage blank{240, 90, QImage::Format_RGB32};
    blank.fill(Qt::white);
    EXPECT_EQ(engine->recognize({.image = blank, .language = OcrLanguage::English}).status, OcrStatus::EmptyText);

    auto const cancelled = engine->recognize({.image = blank, .language = OcrLanguage::English},
                                             std::make_shared<std::atomic_bool>(true));
    EXPECT_EQ(cancelled.status, OcrStatus::Cancelled);
}

/// The Chinese recogniser glues Latin words together, so a mixed page is read twice and each
/// Latin-only line takes the English pass's text; the Chinese line must survive that.
TEST(RapidOcrProbe, AMixedPageKeepsItsSpacesAndItsChinese)
{
    auto engine = engineFor(OcrLanguage::ChineseEnglish);
    if (not engine) GTEST_SKIP() << "LENS_RAPIDOCR_ROOT is not set or the distribution is incomplete";

    QImage image{QString::fromStdWString((corpus() / "browser-mixed-100-light.png").wstring())};
    ASSERT_FALSE(image.isNull());
    auto const result = engine->recognize(
        {.requestId = 21, .source = OcrSource::LocalTest, .image = image, .language = OcrLanguage::ChineseEnglish});

    ASSERT_EQ(result.status, OcrStatus::Ready) << "status " << static_cast<int>(result.status);
    EXPECT_TRUE(result.text.contains(QStringLiteral("The label on the drawer"), Qt::CaseInsensitive)) << result.text.toStdString();
    EXPECT_TRUE(result.text.contains(QStringLiteral("待修"))) << result.text.toStdString();
    EXPECT_TRUE(result.text.contains(QStringLiteral("先量再做"))) << result.text.toStdString();
    ASSERT_EQ(result.attempts.size(), 2u) << "the second pass is the point of this case";
}

TEST(RapidOcrProbe, AZeroDeadlineIsATimeoutWithoutStartingTheProgram)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_RAPIDOCR_ROOT is not set or the distribution is incomplete";

    QImage image{240, 90, QImage::Format_RGB32};
    image.fill(Qt::white);
    auto const result = engine->recognize(
        {.image = image, .language = OcrLanguage::English, .deadline = std::chrono::milliseconds{0}});
    EXPECT_EQ(result.status, OcrStatus::TimedOut);
}

}
