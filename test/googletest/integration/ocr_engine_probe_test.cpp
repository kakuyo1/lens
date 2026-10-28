/**
 * @file ocr_engine_probe_test.cpp
 * @brief The real Tesseract adapter against the committed synthetic corpus.
 *
 * Offline and desktop-free: it reads test/ocr/corpus and runs the installed Tesseract named by
 * LENS_TESSERACT_ROOT, which scripts/qa/gtest.ps1 takes from the merged machine paths. No language
 * pack is installed and no image leaves the process except through the engine's own stdin pipe.
 *
 * Human-run (a real child process is the subject):
 * `powershell -File scripts/qa/gtest.ps1 -Target lens_gtest_integration -Filter 'OcrEngineProbe*'`.
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>

#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include "capture/ocr_engine.h"

namespace {

using lens::capture::OcrLanguage;
using lens::capture::OcrRequest;
using lens::capture::OcrSource;
using lens::capture::OcrStatus;

std::filesystem::path corpus()
{
    return std::filesystem::path{LENS_SOURCE_DIR} / "test" / "ocr" / "corpus";
}

std::unique_ptr<lens::capture::OcrEngine> engineFor(std::optional<OcrLanguage> language)
{
    char const* root = std::getenv("LENS_TESSERACT_ROOT");
    if (root == nullptr or *root == '\0') return {};
    auto const directory = std::filesystem::path{root};
    return lens::capture::makeTesseractOcr({.executable    = QString::fromStdWString((directory / "tesseract.exe").wstring()),
                                            .dataDirectory = QString::fromStdWString((directory / "tessdata").wstring()),
                                            .language      = language});
}

TEST(OcrEngineProbe, ReadsASyntheticEnglishPageWithLinesWordsAndConfidence)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_TESSERACT_ROOT is not set";

    QImage image{QString::fromStdWString((corpus() / "browser-en-100-light.png").wstring())};
    ASSERT_FALSE(image.isNull());
    OcrRequest request{.requestId = 7, .source = OcrSource::LocalTest, .image = image, .language = OcrLanguage::English};
    auto const result = engine->recognize(request);

    ASSERT_EQ(result.status, OcrStatus::Ready) << "status " << static_cast<int>(result.status);
    EXPECT_EQ(result.requestId, 7u);
    EXPECT_EQ(result.actualEngine, QStringLiteral("tesseract"));
    EXPECT_EQ(result.languageUsed, QStringLiteral("eng"));
    EXPECT_TRUE(result.text.contains(QStringLiteral("harbour")));
    EXPECT_FALSE(result.lines.empty());
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Word);
    ASSERT_TRUE(result.confidence);
    EXPECT_GT(result.confidence->words, 0u);
    EXPECT_GT(result.confidence->average, 0.5);
    ASSERT_EQ(result.attempts.size(), 1u);
    EXPECT_EQ(result.attempts[0].status, OcrStatus::Ready);

    for (auto const& line : result.lines) {
        if (line.box) EXPECT_TRUE(lens::capture::boxFitsImage(*line.box, image.size()));
        for (auto const& word : line.words)
            if (word.box) EXPECT_TRUE(lens::capture::boxFitsImage(*word.box, image.size()));
    }
}

TEST(OcrEngineProbe, AnExplicitLanguageThatHasNoDataIsLanguageMissing)
{
    auto engine = engineFor(OcrLanguage::SimplifiedChinese);
    if (not engine) GTEST_SKIP() << "LENS_TESSERACT_ROOT is not set";

    QImage image{QString::fromStdWString((corpus() / "browser-zh-100-light.png").wstring())};
    ASSERT_FALSE(image.isNull());
    auto const result = engine->recognize(OcrRequest{.image = image, .language = OcrLanguage::SimplifiedChinese});
    if (result.status == OcrStatus::Ready) GTEST_SKIP() << "this machine has chi_sim installed; the missing-data path is covered offline";
    EXPECT_EQ(result.status, OcrStatus::LanguageMissing);
}

TEST(OcrEngineProbe, ACancelledRequestIsReportedWithoutRunningTheEngine)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_TESSERACT_ROOT is not set";

    QImage image{200, 80, QImage::Format_RGB32};
    image.fill(Qt::white);
    auto const result = engine->recognize({.image = image, .language = OcrLanguage::English},
                                          std::make_shared<std::atomic_bool>(true));
    EXPECT_EQ(result.status, OcrStatus::Cancelled);
    EXPECT_TRUE(result.text.isEmpty());
}

TEST(OcrEngineProbe, AZeroDeadlineIsATimeoutWithoutRunningTheEngine)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_TESSERACT_ROOT is not set";

    QImage image{200, 80, QImage::Format_RGB32};
    image.fill(Qt::white);
    auto const result = engine->recognize(
        {.image = image, .language = OcrLanguage::English, .deadline = std::chrono::milliseconds{0}});
    EXPECT_EQ(result.status, OcrStatus::TimedOut);
}

TEST(OcrEngineProbe, ABarePageIsEmptyTextAndNotARuntimeFault)
{
    auto engine = engineFor(OcrLanguage::English);
    if (not engine) GTEST_SKIP() << "LENS_TESSERACT_ROOT is not set";

    QImage blank{200, 80, QImage::Format_RGB32};
    blank.fill(Qt::white);
    auto const result = engine->recognize(OcrRequest{.image = blank, .language = OcrLanguage::English});
    EXPECT_EQ(result.status, OcrStatus::EmptyText);
    EXPECT_TRUE(result.text.isEmpty());
}

}
