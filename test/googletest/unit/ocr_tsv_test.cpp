/**
 * @file ocr_tsv_test.cpp
 * @brief The Tesseract TSV contract: strict columns, boxes, limits, confidence and line text.
 *
 * The fixtures are hand-written rows rather than output from a Tesseract run: this suite is
 * offline and must not need a runtime. The corpus baseline (scripts/qa/ocr-bench.ps1) is where the
 * real engine is measured.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <limits>

#include "capture/ocr_engine.h"
#include "capture/ocr_tsv.h"

namespace {

using lens::capture::OcrGeometryLevel;
using lens::capture::OcrLanguage;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;

constexpr char kHeader[] = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext";

QByteArray row(int level, int word, int x, int y, int width, int height, double confidence, QByteArray const& text)
{
    return QByteArray::number(level) + "\t1\t1\t1\t1\t" + QByteArray::number(word) + '\t' + QByteArray::number(x) + '\t' +
           QByteArray::number(y) + '\t' + QByteArray::number(width) + '\t' + QByteArray::number(height) + '\t' +
           QByteArray::number(confidence, 'f', 4) + '\t' + text + "\r\n";
}

QByteArray tsv(std::initializer_list<QByteArray> rows)
{
    QByteArray out = QByteArray{kHeader} + "\r\n";
    for (auto const& entry : rows)
        out += entry;
    return out;
}

}

TEST(OcrTsv, WordsBecomeLinesWithBoxesAndCoveredConfidence)
{
    auto const document = tsv({row(4, 0, 10, 10, 80, 20, -1, ""),
                               row(5, 1, 10, 10, 30, 20, 95.0, "Hello"),
                               row(5, 2, 46, 10, 30, 20, 85.0, "world")});
    auto const result   = lens::capture::parseTesseractTsv(document, QSize{200, 100});

    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("Hello world"));
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0].text, QStringLiteral("Hello world"));
    EXPECT_EQ(result.lines[0].box->width, 80);
    EXPECT_EQ(result.lines[0].words.size(), 2u);
    EXPECT_EQ(result.lines[0].words[0].text, QStringLiteral("Hello"));
    EXPECT_DOUBLE_EQ(result.lines[0].words[1].box->x, 46);
    EXPECT_EQ(result.geometryLevel, OcrGeometryLevel::Word);
    ASSERT_TRUE(result.confidence);
    EXPECT_DOUBLE_EQ(result.confidence->average, 0.9);
    EXPECT_EQ(result.confidence->words, 2u);
    EXPECT_EQ(result.confidence->total, 2u);
}

TEST(OcrTsv, ChineseWordsAreJoinedWithoutAFabricatedSpace)
{
    // 2 px between two 18 px tall glyph boxes is a gap inside a word, not a word separator.
    auto const document = tsv({row(5, 1, 10, 10, 18, 18, 90.0, QStringLiteral("清晨").toUtf8()),
                               row(5, 2, 30, 10, 18, 18, 90.0, QStringLiteral("码头").toUtf8())});
    auto const result   = lens::capture::parseTesseractTsv(document, QSize{200, 100});

    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("清晨码头"));
}

TEST(OcrTsv, ALargeGapKeepsTheSpaceTheEngineCounted)
{
    auto const document = tsv({row(5, 1, 10, 10, 18, 18, 90.0, QStringLiteral("待修").toUtf8()),
                               row(5, 2, 40, 10, 18, 18, 90.0, QStringLiteral("pending").toUtf8())});
    auto const result   = lens::capture::parseTesseractTsv(document, QSize{200, 100});

    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("待修 pending"));
}

TEST(OcrTsv, MissingConfidenceIsReportedAsMissingNotAsCertainty)
{
    auto const document = tsv({row(5, 1, 10, 10, 30, 20, -1, "hello")});
    auto const result   = lens::capture::parseTesseractTsv(document, QSize{200, 100});

    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_FALSE(result.confidence);
}

TEST(OcrTsv, AMalformedColumnCountIsAProtocolError)
{
    auto document = tsv({});
    document += "5\t1\t1\t1\t1\t1\t10\t10\t30\t20\t95\r\n";
    EXPECT_EQ(lens::capture::parseTesseractTsv(document, QSize{200, 100}).status, OcrStatus::ProtocolError);
}

TEST(OcrTsv, AMissingHeaderIsAProtocolError)
{
    EXPECT_EQ(lens::capture::parseTesseractTsv("5\t1\t1\t1\t1\t1\t10\t10\t30\t20\t95\thello\r\n", QSize{200, 100}).status,
              OcrStatus::ProtocolError);
}

TEST(OcrTsv, ABomIsAProtocolError)
{
    auto document = tsv({row(5, 1, 10, 10, 30, 20, 95, "hello")});
    document.prepend("\xEF\xBB\xBF");
    EXPECT_EQ(lens::capture::parseTesseractTsv(document, QSize{200, 100}).status, OcrStatus::ProtocolError);
}

TEST(OcrTsv, ABoxOutsideTheCropIsAProtocolError)
{
    auto const document = tsv({row(5, 1, 90, 10, 30, 20, 95, "hello")});
    EXPECT_EQ(lens::capture::parseTesseractTsv(document, QSize{100, 100}).status, OcrStatus::ProtocolError);
}

TEST(OcrTsv, AConfidenceOutsideZeroToHundredIsAProtocolError)
{
    auto const document = tsv({row(5, 1, 10, 10, 30, 20, 101, "hello")});
    EXPECT_EQ(lens::capture::parseTesseractTsv(document, QSize{200, 100}).status, OcrStatus::ProtocolError);
}

TEST(OcrTsv, InvalidUtf8IsAProtocolErrorAndCarriesNoText)
{
    auto const document = tsv({row(5, 1, 10, 10, 30, 20, 95, QByteArray{"he\xFFllo"})});
    auto const result   = lens::capture::parseTesseractTsv(document, QSize{200, 100});
    EXPECT_EQ(result.status, OcrStatus::ProtocolError);
    EXPECT_TRUE(result.text.isEmpty());
}

TEST(OcrTsv, NoWordsIsEmptyTextNotSuccess)
{
    auto const document = tsv({row(4, 0, 10, 10, 80, 20, -1, "")});
    EXPECT_EQ(lens::capture::parseTesseractTsv(document, QSize{200, 100}).status, OcrStatus::EmptyText);
}

TEST(OcrTsv, MoreThanTwentyThousandWordsIsAResultTooLarge)
{
    QByteArray document = QByteArray{kHeader} + "\r\n";
    for (int index = 0; index < 20001; ++index)
        document += row(5, index, 0, 0, 1, 1, 90, "x");
    EXPECT_EQ(lens::capture::parseTesseractTsv(document, QSize{100, 100}).status, OcrStatus::ResultTooLarge);
}

TEST(TesseractLanguages, AnExplicitPolicyNamesWhatIsMissing)
{
    QTemporaryDir english;
    QTemporaryDir both;
    ASSERT_TRUE(english.isValid());
    ASSERT_TRUE(both.isValid());
    auto write = [](QString const& directory, char const* code) {
        QFile file{QDir{directory}.filePath(QString::fromLatin1(code) + QStringLiteral(".traineddata"))};
        if (not file.open(QIODevice::WriteOnly)) return false;
        return file.write("x", 1) == 1;
    };
    ASSERT_TRUE(write(english.path(), "eng"));
    ASSERT_TRUE(write(both.path(), "eng"));
    ASSERT_TRUE(write(both.path(), "chi_sim"));

    auto const en = lens::capture::tesseractLanguageFor(OcrLanguage::English, english.path());
    EXPECT_TRUE(en.ok());
    EXPECT_EQ(en.spec, QStringLiteral("eng"));

    auto const zh = lens::capture::tesseractLanguageFor(OcrLanguage::SimplifiedChinese, english.path());
    EXPECT_FALSE(zh.ok());
    EXPECT_EQ(zh.missing, QStringLiteral("chi_sim"));

    auto const mixedMissing = lens::capture::tesseractLanguageFor(OcrLanguage::ChineseEnglish, english.path());
    EXPECT_FALSE(mixedMissing.ok());
    EXPECT_EQ(mixedMissing.missing, QStringLiteral("chi_sim"));

    auto const mixed = lens::capture::tesseractLanguageFor(OcrLanguage::ChineseEnglish, both.path());
    EXPECT_TRUE(mixed.ok());
    EXPECT_EQ(mixed.spec, QStringLiteral("chi_sim+eng"));
}

TEST(TesseractLanguages, TheLanguageKeyRoundTrips)
{
    for (auto const language : {OcrLanguage::English, OcrLanguage::SimplifiedChinese, OcrLanguage::ChineseEnglish}) {
        auto const key = lens::capture::ocrLanguageKey(language);
        EXPECT_EQ(lens::capture::parseOcrLanguage(key.toStdString()), language);
    }
    EXPECT_FALSE(lens::capture::parseOcrLanguage("auto"));
}

TEST(OcrBox, BoundsAreFiniteNonNegativeAndInsideTheCrop)
{
    EXPECT_TRUE(lens::capture::boxFitsImage({.x = 0, .y = 0, .width = 10, .height = 10}, QSize{10, 10}));
    EXPECT_FALSE(lens::capture::boxFitsImage({.x = -1, .y = 0, .width = 10, .height = 10}, QSize{10, 10}));
    EXPECT_FALSE(lens::capture::boxFitsImage({.x = 0, .y = 0, .width = 11, .height = 10}, QSize{10, 10}));
    EXPECT_FALSE(lens::capture::boxFitsImage({.x = 0, .y = 0, .width = 0, .height = 10}, QSize{10, 10}));
    EXPECT_FALSE(lens::capture::boxFitsImage({.x = 0, .y = 0, .width = std::numeric_limits<double>::infinity(), .height = 10},
                                             QSize{10, 10}));
}
