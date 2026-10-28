/**
 * @file ocr_local_test.cpp
 * @brief Local engine policy, language resources and the one-time migration rule.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>

#include "capture/ocr_local.h"
#include "capture/ocr_migration.h"

namespace {

using lens::capture::LocalEngineChoice;
using lens::capture::LocalOcrConfig;
using lens::capture::OcrLanguage;
using lens::capture::OcrStatus;

bool writeFile(QString const& path)
{
    QFile file{path};
    if (not file.open(QIODevice::WriteOnly)) return false;
    return file.write("x", 1) == 1;
}

struct Runtime {
    QTemporaryDir root;
    QString       executable;
    QString       data;

    Runtime()
    {
        executable = QDir{root.path()}.filePath(QStringLiteral("tesseract.exe"));
        data       = QDir{root.path()}.filePath(QStringLiteral("tessdata"));
        QDir{}.mkpath(data);
        writeFile(executable);
    }

    void language(char const* code) const
    {
        writeFile(QDir{data}.filePath(QString::fromLatin1(code) + QStringLiteral(".traineddata")));
    }
};

}

TEST(OcrLocal, AutoUsesTesseractWhileWindowsDeploymentIsNotUsable)
{
    Runtime runtime;
    runtime.language("eng");
    auto const inspection = lens::capture::inspectLocalOcr(
        {.engine    = LocalEngineChoice::Auto,
         .language  = OcrLanguage::English,
         .tesseract = {.executable = runtime.executable, .dataDirectory = runtime.data}});

    EXPECT_EQ(inspection.status, OcrStatus::Ready);
    EXPECT_EQ(inspection.engine, QStringLiteral("tesseract"));
    EXPECT_EQ(inspection.windowsReason, QStringLiteral("deployment-unsupported"));
}

TEST(OcrLocal, ExplicitWindowsReportsTheDeploymentGateAndNeverFallsBack)
{
    auto const inspection = lens::capture::inspectLocalOcr({.engine = LocalEngineChoice::Windows});
    EXPECT_EQ(inspection.status, OcrStatus::DeploymentUnsupported);
    EXPECT_EQ(inspection.engine, QStringLiteral("windows"));

    auto const engine = lens::capture::makeLocalOcr({.engine = LocalEngineChoice::Windows});
    ASSERT_TRUE(engine);
    EXPECT_EQ(engine->probe(), OcrStatus::DeploymentUnsupported);
}

TEST(OcrLocal, TheChosenLanguageNamesTheMissingTrainedData)
{
    Runtime runtime;
    runtime.language("eng");
    auto const inspection = lens::capture::inspectLocalOcr(
        {.engine    = LocalEngineChoice::Tesseract,
         .language  = OcrLanguage::SimplifiedChinese,
         .tesseract = {.executable = runtime.executable, .dataDirectory = runtime.data}});

    EXPECT_EQ(inspection.status, OcrStatus::LanguageMissing);
    EXPECT_EQ(inspection.reason, QStringLiteral("chi_sim"));
}

TEST(OcrLocal, MixedRecognitionNeedsBothFilesAndDoesNotDropOne)
{
    Runtime chineseOnly;
    chineseOnly.language("chi_sim");
    auto const missing = lens::capture::inspectLocalOcr(
        {.engine    = LocalEngineChoice::Tesseract,
         .language  = OcrLanguage::ChineseEnglish,
         .tesseract = {.executable = chineseOnly.executable, .dataDirectory = chineseOnly.data}});
    EXPECT_EQ(missing.status, OcrStatus::LanguageMissing);
    EXPECT_EQ(missing.reason, QStringLiteral("eng"));

    chineseOnly.language("eng");
    auto const ready = lens::capture::inspectLocalOcr(
        {.engine    = LocalEngineChoice::Tesseract,
         .language  = OcrLanguage::ChineseEnglish,
         .tesseract = {.executable = chineseOnly.executable, .dataDirectory = chineseOnly.data}});
    EXPECT_EQ(ready.status, OcrStatus::Ready);
}

TEST(OcrLocal, AUserInstalledEngineIsATesseractChoiceAndKeepsItsLanguage)
{
    Runtime runtime;
    runtime.language("chi_sim");
    auto const inspection = lens::capture::inspectLocalOcr(
        {.engine    = LocalEngineChoice::Tesseract,
         .language  = OcrLanguage::SimplifiedChinese,
         .tesseract = {.executable = runtime.executable, .dataDirectory = runtime.data}});
    EXPECT_EQ(inspection.status, OcrStatus::Ready);
    EXPECT_EQ(inspection.engine, QStringLiteral("tesseract"));
}

TEST(OcrMigration, AFreshDocumentUsesAutoAndEnglish)
{
    auto const decision = lens::capture::decideOcrMigration({.version = 0});
    ASSERT_TRUE(decision.migrate);
    EXPECT_EQ(decision.version, 1);
    EXPECT_EQ(decision.engine, LocalEngineChoice::Auto);
    EXPECT_EQ(decision.language, OcrLanguage::English);
}

TEST(OcrMigration, ChineseDataBesideTheBundledRuntimeSurvivesAuto)
{
    auto const mixed = lens::capture::decideOcrMigration(
        {.version = 0, .customTesseract = false, .englishData = true, .chineseData = true});
    EXPECT_EQ(mixed.engine, LocalEngineChoice::Auto);
    EXPECT_EQ(mixed.language, OcrLanguage::ChineseEnglish);

    auto const chinese = lens::capture::decideOcrMigration(
        {.version = 0, .customTesseract = false, .englishData = false, .chineseData = true});
    EXPECT_EQ(chinese.engine, LocalEngineChoice::Auto);
    EXPECT_EQ(chinese.language, OcrLanguage::SimplifiedChinese);
}

TEST(OcrMigration, AnInstalledEngineKeepsItsExplicitChoice)
{
    auto const english = lens::capture::decideOcrMigration(
        {.version = 0, .customTesseract = true, .englishData = true, .chineseData = false});
    EXPECT_EQ(english.engine, LocalEngineChoice::Tesseract);
    EXPECT_EQ(english.language, OcrLanguage::English);

    auto const chinese = lens::capture::decideOcrMigration(
        {.version = 0, .customTesseract = true, .englishData = false, .chineseData = true});
    EXPECT_EQ(chinese.engine, LocalEngineChoice::Tesseract);
    EXPECT_EQ(chinese.language, OcrLanguage::SimplifiedChinese);

    auto const both = lens::capture::decideOcrMigration(
        {.version = 0, .customTesseract = true, .englishData = true, .chineseData = true});
    EXPECT_EQ(both.engine, LocalEngineChoice::Tesseract);
    EXPECT_EQ(both.language, OcrLanguage::ChineseEnglish);
}

TEST(OcrMigration, AMigratedDocumentIsNeverGuessedAgain)
{
    auto const decision = lens::capture::decideOcrMigration(
        {.version = 1, .customTesseract = true, .englishData = true, .chineseData = true});
    EXPECT_FALSE(decision.migrate);
    EXPECT_EQ(decision.version, 1);
}
