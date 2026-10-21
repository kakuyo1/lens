/**
 * @file settings_defaults_test.cpp
 * @brief Restore-defaults and a fresh install read the same table.
 */

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <gtest/gtest.h>

#include <filesystem>

#include "app/app_controller.h"
#include "app/platform/global_hotkey.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/settings_defaults.h"
#include "core/document/known_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "llm_support.h"
#include "support.h"

namespace {

using namespace lens;
using lens::test::LlmTest;

struct QtApplication {
    int              argc     = 1;
    char             name[32] = "settings_defaults_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

/// @return An answer that never arrives, so the free model-list GET stays off the network.
class QuietReply final : public QNetworkReply {
public:
    explicit QuietReply(QObject* parent)
        : QNetworkReply(parent)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
    }

    void abort() override
    {}

protected:
    qint64 readData(char*, qint64) override
    {
        return -1;
    }
};

class QuietManager final : public QNetworkAccessManager {
protected:
    QNetworkReply* createRequest(Operation, QNetworkRequest const&, QIODevice*) override
    {
        auto* reply = new QuietReply(this);
        reply->setParent(this);
        return reply;
    }
};

const llm::Pricing& pricing()
{
    static const llm::Pricing loaded = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    return loaded;
}

} // namespace

TEST_F(LlmTest, RestoreDefaultsMatchesAFreshInstallAndLeavesOwnedStateAlone)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path{temporary.path().toStdWString()};

    auto storeA = core::KnownStore::load(root / "a.json");
    auto storeB = core::KnownStore::load(root / "b.json");

    app::MouseSelectionHook hookA;
    app::MouseSelectionHook hookB;
    app::GlobalHotkey       hotkeyA;
    app::GlobalHotkey       hotkeyB;
    QuietManager            managerA;
    QuietManager            managerB;
    auto const              config = [] {
        return llm::Config{QUrl{app::defaults::defaultBaseUrl()}, {}, app::defaults::defaultModelName()};
    };
    llm::LlmClient clientA{config(), &managerA};
    llm::LlmClient clientB{config(), &managerB};

    app::AppController a{storeA, clientA, hookA, hotkeyA, pricing(), test::sourceDir() / "data"};
    app::AppController b{storeB, clientB, hookB, hotkeyB, pricing(), test::sourceDir() / "data"};

    a.setLevel(5);
    a.setExplanationLang(QStringLiteral("ja"));
    a.setMultiSense(true);
    a.setEtymology(true);
    a.setExample(true);
    a.setTheme(QStringLiteral("dark"));
    a.setAnimationsEnabled(false);
    a.setRevealSpeed(1);
    a.setSelectionCapture(false);
    a.setClipboardPolicy(true);
    a.setBubbleDismissSeconds(12);
    a.setBubblePlacement(QStringLiteral("below"));
    a.setSelectionBarStyle(QStringLiteral("fan"));
    a.setFrostedBlur(50);
    a.setFrostedSaturation(10);
    a.setFrostedRim(90);
    a.setLiquidRefraction(2);
    a.setLiquidBlur(90);
    a.setLiquidRim(2);
    a.setLiquidTextColor(QStringLiteral("#ffffff"));
    a.setGlassQuality(static_cast<int>(app::GlassQuality::Smooth));
    ASSERT_EQ(a.glassBackdrop()->quality(), app::GlassQuality::Smooth);
    a.setReviewFrequency(static_cast<int>(core::ReviewFrequency::Every1To2Hours));
    ASSERT_TRUE(a.captureDuty()->setScanWhitelist(QStringLiteral("notepad.exe")));
    a.captureDuty()->setAutoScan(true);
    a.captureDuty()->setOcrCapture(true);
    a.captureDuty()->setMinimumWordLength(5);
    EXPECT_TRUE(a.setOcrHotkey(Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier).isEmpty());
    ASSERT_TRUE(a.modelCatalogDuty()->setProvider(QStringLiteral("openai")));
    ASSERT_TRUE(a.modelCatalogDuty()->setModel(QStringLiteral("gpt-4.1-nano")));

    a.restoreDefaults();

    // The probe status is the one asynchronous field: it may read "checking" on one controller
    // and "runtime-missing" on the other whatever restore wrote.
    const auto projection = [](QVariantMap map) {
        map.remove(QStringLiteral("ocrStatus"));
        return map;
    };
    EXPECT_EQ(a.glassBackdrop()->quality(), app::GlassQuality::Auto);
    const auto restored = projection(a.settings());
    const auto fresh    = projection(b.settings());
    EXPECT_EQ(restored.keys(), fresh.keys());
    for (auto it = restored.cbegin(); it != restored.cend(); ++it) {
        EXPECT_EQ(it.value(), fresh.value(it.key())) << "the restored setting differs: " << it.key().toStdString();
    }

    // The API key, autostart, interface language and daily budget are not the panel's defaults.
    a.setUiLanguage(QStringLiteral("ja"));
    a.setApiKey(QStringLiteral("fake-test-token"));
    ASSERT_TRUE(a.setDailyBudget(1.25));
    a.restoreDefaults();
    EXPECT_EQ(a.settings().value("uiLanguage").toString(), QStringLiteral("ja"));
    EXPECT_TRUE(a.settings().value("hasApiKey").toBool());
    EXPECT_DOUBLE_EQ(a.settings().value("dailyBudget").toDouble(), 1.25);
}

TEST(GlassQuality, StoredValuesClampToTheFourTiers)
{
    // A hand-edited document can hold anything; the four tiers are the only values the refresh understands.
    EXPECT_EQ(app::glassQualityFrom(-3), app::GlassQuality::Auto);
    EXPECT_EQ(app::glassQualityFrom(0), app::GlassQuality::Auto);
    EXPECT_EQ(app::glassQualityFrom(3), app::GlassQuality::Smooth);
    EXPECT_EQ(app::glassQualityFrom(9), app::GlassQuality::Smooth);
    EXPECT_TRUE(app::glassQualityDuplicates(app::GlassQuality::Auto));
    EXPECT_FALSE(app::glassQualityDuplicates(app::GlassQuality::Standard));
    EXPECT_FALSE(app::glassQualityRefreshes(app::GlassQuality::Eco));
    EXPECT_TRUE(app::glassQualityRefreshes(app::GlassQuality::Standard));
}

TEST(GlassQuality, SmoothFollowsTheScreenWhileAutomaticKeepsTheSixtyHertzCap)
{
    using app::GlassQuality;
    // The default tier is bounded whatever the screen can do; the top tier spends the extra reads to match it.
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Auto, 144.0), app::kGlassAutoFrameMs);
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Smooth, 144.0), 6);
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Smooth, 165.0), 6);
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Smooth, 60.0), 16);
    // An unknown rate falls back to the conservative pace rather than a poll with no frame behind it.
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Smooth, 0.0), app::kGlassAutoFrameMs);
    // A 1000 Hz report is a floor, not a reason to poll a thousand times: the copy has to fit inside the tick.
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Smooth, 1000.0), app::kGlassFrameFloorMs);
    // A slow screen is polled no slower than the GDI budget the other sheets share.
    EXPECT_EQ(app::glassQualityFrameMs(GlassQuality::Smooth, 10.0), app::kGlassGdiFrameMs);
}
