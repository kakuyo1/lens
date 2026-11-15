/** @file ocr_engine_test.cpp
 * @brief The tesseract engine's own success path, driven end to end through a stub binary.
 *
 * Every other case here exercises the parser or the executor seams; none of them ran the engine
 * itself, which is why the run that completes -- the drain, the exit check, the timing fields --
 * had no coverage. The stub (lens_ocr_stub, built beside this suite) drains the image from stdin
 * and answers with one confident word, which is all the success path needs from tesseract.
 */
#include <QDir>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <fstream>

#include "capture/ocr_engine.h"

#ifndef LENS_OCR_STUB
#define LENS_OCR_STUB ""
#endif

namespace {

TEST(OcrEngine, ACompletedStubRunCarriesTheRecognizedTextAndItsTiming)
{
    QTemporaryDir tessdata;
    ASSERT_TRUE(QDir{tessdata.path()}.mkpath("."));
    {
        // The language check reads the traineddata's existence and size, never its contents --
        // the stub run is the one that answers, and it needs only a readable marker file.
        std::ofstream marker(tessdata.filePath(QStringLiteral("eng.traineddata")).toStdString(),
                             std::ios::binary);
        marker << 'x';
    }

    lens::capture::TesseractConfig config;
    config.executable    = QStringLiteral(LENS_OCR_STUB);
    config.dataDirectory = tessdata.path();
    config.language      = lens::capture::OcrLanguage::English;

    QImage image{8, 8, QImage::Format_Grayscale8};
    image.fill(Qt::white);

    lens::capture::OcrRequest request;
    request.source    = lens::capture::OcrSource::LocalTest;
    request.image     = image;
    request.language  = lens::capture::OcrLanguage::English;
    request.requestId = 7;

    auto const engine = lens::capture::makeTesseractOcr(config);
    ASSERT_NE(engine, nullptr);
    auto const result = engine->recognize(request);

    EXPECT_EQ(result.status, lens::capture::OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("stub"));
    EXPECT_EQ(result.actualEngine, QStringLiteral("tesseract"));
    EXPECT_EQ(result.requestId, 7u);
    ASSERT_FALSE(result.attempts.empty());
    EXPECT_EQ(result.attempts.front().engine, QStringLiteral("tesseract"));
    EXPECT_EQ(result.attempts.front().status, lens::capture::OcrStatus::Ready);
}

} // namespace
