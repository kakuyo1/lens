/**
 * @file ocr_rapidocr_test.cpp
 * @brief RapidOCR path policy, trust digest and the strict response parser.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "capture/ocr_local.h"
#include "capture/ocr_rapidocr.h"

namespace {

using lens::capture::LocalEngineChoice;
using lens::capture::LocalOcrConfig;
using lens::capture::OcrLanguage;
using lens::capture::OcrStatus;
using lens::capture::RapidOcrConfig;

bool writeFile(QString const& path, QByteArray const& bytes = QByteArrayLiteral("x"))
{
    QFile file{path};
    if (not file.open(QIODevice::WriteOnly)) return false;
    return file.write(bytes) == bytes.size();
}

QByteArray successResponse(QString const& text = QStringLiteral("Lens"))
{
    QJsonObject block;
    block.insert(QStringLiteral("text"), text);
    block.insert(QStringLiteral("box"),
                 QJsonArray{QJsonArray{10, 10}, QJsonArray{90, 10}, QJsonArray{90, 30}, QJsonArray{10, 30}});
    block.insert(QStringLiteral("score"), 0.9);
    QJsonObject root;
    root.insert(QStringLiteral("code"), 100);
    root.insert(QStringLiteral("data"), QJsonArray{block});
    return QJsonDocument{root}.toJson(QJsonDocument::Compact);
}

struct Distribution {
    QTemporaryDir root;
    QString       executable;
    QString       models;

    Distribution()
    {
        executable = QDir{root.path()}.filePath(QStringLiteral("RapidOCR-json.exe"));
        models     = QDir{root.path()}.filePath(QStringLiteral("models"));
        QDir{}.mkpath(models);
        writeFile(executable, QByteArrayLiteral("exe"));
        for (auto const& language : {OcrLanguage::English, OcrLanguage::SimplifiedChinese}) {
            for (auto const& name : lens::capture::rapidOcrModelFiles(language))
                writeFile(QDir{models}.filePath(name), name.toUtf8());
        }
    }

    RapidOcrConfig config(OcrLanguage const language = OcrLanguage::English) const
    {
        return {.executable = executable, .modelsDirectory = models, .language = language};
    }
};

}

TEST(OcrRapidOcr, AnUnchosenDistributionIsUntrustedAndDoesNotRun)
{
    Distribution distribution;
    auto const   inspection = lens::capture::inspectRapidOcr(distribution.config(), true);
    EXPECT_EQ(inspection.status, OcrStatus::Untrusted);
    EXPECT_EQ(inspection.reason, QStringLiteral("untrusted"));

    auto engine = lens::capture::makeRapidOcr(distribution.config());
    ASSERT_TRUE(engine);
    EXPECT_EQ(engine->probe(), OcrStatus::Untrusted);
}

TEST(OcrRapidOcr, ATrustedDigestBecomesReadyAndChangesWhenAModelChanges)
{
    Distribution distribution;
    auto         config = distribution.config();
    config.trustDigest  = lens::capture::rapidOcrTrustDigest(config);
    ASSERT_FALSE(config.trustDigest.isEmpty());

    EXPECT_EQ(lens::capture::inspectRapidOcr(config, true).status, OcrStatus::Ready);
    auto engine = lens::capture::makeRapidOcr(config);
    ASSERT_TRUE(engine);
    EXPECT_EQ(engine->probe(), OcrStatus::Ready);

    auto const dict = QDir{distribution.models}.filePath(QStringLiteral("dict_en.txt"));
    ASSERT_TRUE(writeFile(dict, QByteArrayLiteral("changed")));
    auto const changed = lens::capture::inspectRapidOcr(config, true);
    EXPECT_EQ(changed.status, OcrStatus::Untrusted);
    EXPECT_EQ(changed.reason, QStringLiteral("trust-changed"));
    EXPECT_NE(lens::capture::rapidOcrTrustDigest(config), config.trustDigest);
}

TEST(OcrRapidOcr, ANonAsciiModelFolderIsRejectedBeforeAnyProcessCanStart)
{
    Distribution distribution;
    auto         config    = distribution.config();
    config.modelsDirectory = QDir{distribution.root.path()}.filePath(QStringLiteral("模型"));
    QDir{}.mkpath(config.modelsDirectory);

    auto const inspection = lens::capture::inspectRapidOcr(config, true);
    EXPECT_EQ(inspection.status, OcrStatus::PathUnsupported);
    EXPECT_EQ(inspection.reason, QStringLiteral("path-unsupported"));

    auto engine = lens::capture::makeRapidOcr(config);
    ASSERT_TRUE(engine);
    EXPECT_EQ(engine->probe(), OcrStatus::PathUnsupported);
    EXPECT_FALSE(lens::capture::rapidOcrPathIsAscii(config.modelsDirectory));
    EXPECT_TRUE(lens::capture::rapidOcrPathIsAscii(QStringLiteral("C:/RapidOCR/models")));
}

TEST(OcrRapidOcr, ATrustedDistributionStillRequiresEverySelectedModelFile)
{
    Distribution distribution;
    auto         config = distribution.config(OcrLanguage::SimplifiedChinese);
    config.trustDigest  = lens::capture::rapidOcrTrustDigest(config);
    ASSERT_FALSE(config.trustDigest.isEmpty());
    ASSERT_TRUE(QFile::remove(QDir{distribution.models}.filePath(QStringLiteral("ch_PP-OCRv3_rec_infer.onnx"))));

    auto const inspection = lens::capture::inspectRapidOcr(config, true);
    EXPECT_EQ(inspection.status, OcrStatus::LanguageMissing);
    EXPECT_EQ(inspection.reason, QStringLiteral("ch_PP-OCRv3_rec_infer.onnx"));
}

TEST(OcrRapidOcr, AutoNeverChoosesTheUserInstalledEngine)
{
    Distribution distribution;
    auto         rapid    = distribution.config();
    rapid.trustDigest     = lens::capture::rapidOcrTrustDigest(rapid);
    auto const inspection = lens::capture::inspectLocalOcr(
        {.engine    = LocalEngineChoice::Auto,
         .language  = OcrLanguage::English,
         .tesseract = {.executable = distribution.executable, .dataDirectory = distribution.models},
         .rapidOcr  = rapid});
    EXPECT_NE(inspection.engine, QStringLiteral("rapidocr"));
}

TEST(OcrRapidOcr, AResponseCarriesBlockLinesAndAxisAlignedBoxesButNoFakeWordBoxes)
{
    auto const response = successResponse(QStringLiteral("Lens 识别"));
    auto const result   = lens::capture::parseRapidOcrResponse(response, QSize{100, 60}, 7, QStringLiteral("zh-Hans+en"), QStringLiteral("rapidocr-json"), 42);

    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.requestId, 7u);
    EXPECT_EQ(result.actualEngine, QStringLiteral("rapidocr-json"));
    EXPECT_EQ(result.languageUsed, QStringLiteral("zh-Hans+en"));
    EXPECT_EQ(result.timing.totalMs, 42);
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Line);
    EXPECT_FALSE(result.confidence);
    EXPECT_EQ(result.text, QStringLiteral("Lens 识别"));
    ASSERT_EQ(result.lines.size(), 1u);
    ASSERT_TRUE(result.lines[0].box);
    EXPECT_DOUBLE_EQ(result.lines[0].box->x, 10);
    EXPECT_DOUBLE_EQ(result.lines[0].box->y, 10);
    EXPECT_DOUBLE_EQ(result.lines[0].box->width, 80);
    EXPECT_DOUBLE_EQ(result.lines[0].box->height, 20);
    EXPECT_TRUE(result.lines[0].words.empty());
}

TEST(OcrRapidOcr, NoTextIsEmptyTextNotAFailure)
{
    auto const result = lens::capture::parseRapidOcrResponse(
        QByteArrayLiteral(R"({"code":101,"data":"No text found in image."})"), QSize{100, 60}, 1, QStringLiteral("en"), QStringLiteral("rapidocr-json"), 3);
    EXPECT_EQ(result.status, OcrStatus::EmptyText);
    EXPECT_TRUE(result.text.isEmpty());
}

TEST(OcrRapidOcr, AnErrorCodeOrMalformedShapeIsAProtocolErrorWithoutText)
{
    for (auto const& response : {QByteArrayLiteral(R"({"code":205,"data":"No image"})"),
                                 QByteArrayLiteral(R"({not json)"),
                                 QByteArrayLiteral(R"({"code":100,"data":{}})"),
                                 QByteArrayLiteral(R"({"code":100,"data":[{"text":"x","box":[[0,0]],"score":0.9}]})"),
                                 QByteArrayLiteral(R"({"code":100,"data":[{"text":"x","box":[[0,0],[10,0],[10,10],[0,10]]}]})"),
                                 QByteArrayLiteral(R"({"code":100,"data":[{"text":"x","box":[[0,0],[200,0],[200,10],[0,10]],"score":0.9}]})"),
                                 QByteArrayLiteral(R"({"code":100,"data":[{"text":"x","box":[[0,0],[10,0],[10,10],[0,10]],"score":1.5}]})")}) {
        auto const result = lens::capture::parseRapidOcrResponse(response, QSize{100, 60}, 1, QStringLiteral("en"), QStringLiteral("rapidocr-json"), 3);
        EXPECT_EQ(result.status, OcrStatus::ProtocolError) << response.constData();
        EXPECT_TRUE(result.text.isEmpty());
    }
}

TEST(OcrRapidOcr, EmptyDataIsEmptyTextAndAnOversizedBlockIsRefused)
{
    auto const empty = lens::capture::parseRapidOcrResponse(QByteArrayLiteral(R"({"code":100,"data":[]})"),
                                                            QSize{100, 60},
                                                            1,
                                                            QStringLiteral("en"),
                                                            QStringLiteral("rapidocr-json"),
                                                            3);
    EXPECT_EQ(empty.status, OcrStatus::EmptyText);

    QJsonObject block;
    block.insert(QStringLiteral("text"), QString(65537, QLatin1Char('a')));
    block.insert(QStringLiteral("box"),
                 QJsonArray{QJsonArray{10, 10}, QJsonArray{90, 10}, QJsonArray{90, 30}, QJsonArray{10, 30}});
    block.insert(QStringLiteral("score"), 0.9);
    QJsonObject root;
    root.insert(QStringLiteral("code"), 100);
    root.insert(QStringLiteral("data"), QJsonArray{block});
    auto const oversized = lens::capture::parseRapidOcrResponse(QJsonDocument{root}.toJson(QJsonDocument::Compact),
                                                                QSize{100, 60},
                                                                1,
                                                                QStringLiteral("en"),
                                                                QStringLiteral("rapidocr-json"),
                                                                3);
    EXPECT_EQ(oversized.status, OcrStatus::ResultTooLarge);
}

TEST(OcrRapidOcr, ADifferentLanguageSelectsTheEnglishOrChineseModelNames)
{
    auto const english = lens::capture::rapidOcrModelFiles(OcrLanguage::English);
    auto const chinese = lens::capture::rapidOcrModelFiles(OcrLanguage::SimplifiedChinese);
    EXPECT_TRUE(english.contains(QStringLiteral("rec_en_PP-OCRv3_infer.onnx")));
    EXPECT_TRUE(english.contains(QStringLiteral("dict_en.txt")));
    EXPECT_TRUE(chinese.contains(QStringLiteral("ch_PP-OCRv3_rec_infer.onnx")));
    EXPECT_TRUE(chinese.contains(QStringLiteral("dict_chinese.txt")));
}
