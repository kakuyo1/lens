/**
 * @file settings_table_test.cpp
 * @brief The settings table is the one place a setting is declared: its key, its default, its clamp,
 *        its members, the signal its write raises, and whether a sync carries it.
 *
 * Every case here is about the table rather than about one setting: a row added to
 * core/domain/settings_table.h is checked by these cases without a line being written for it, and
 * a row that drifts from what the rest of the application does fails one.
 */

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <set>
#include <string>

#include "app/app_controller.h"
#include "app/platform/global_hotkey.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/settings_access.h"
#include "app/settings_defaults.h"
#include "core/dev/dev_command.h"
#include "core/document/known_store.h"
#include "core/domain/settings_table.h"
#include "core/domain/sync_state.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "llm_support.h"
#include "support.h"

namespace {

using namespace lens;
using lens::test::LlmTest;
using lens::core::settings::Kind;
using lens::core::settings::Row;
using lens::core::settings::Signal;

struct QtApplication {
    int              argc     = 1;
    char             name[32] = "settings_table_test";
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

/// The suite's own name for the fixture: the wire protocol and the filter tables, which a
/// controller needs, and nothing else.
struct SettingsTableRig : LlmTest {};

/// A controller over a settings document of its own, and the document it writes.
struct Rig {
    explicit Rig(std::filesystem::path const& root)
        : store(core::KnownStore::load(root / "settings.json")),
          client(llm::Config{QUrl{app::defaults::defaultBaseUrl()}, {}, app::defaults::defaultModelName()}, &manager),
          controller(store, client, hook, hotkey, pricing(), test::sourceDir() / "data")
    {}

    /// @return The row's value as the document holds it, which is what a restore writes.
    /// @note The two the store holds are read from the store: save() writes its own copy of the
    ///       document out and leaves the in-memory one behind, which is why the projection reads
    ///       those keys from KnownStore too.
    QVariant stored(Row const& row)
    {
        if (row.signal == Signal::Level)
            return store.level();
        if (row.signal == Signal::ExplanationLang)
            return QString::fromStdString(store.explanationLang());
        return app::settings::read(store.section(), row);
    }

    core::KnownStore        store;
    app::MouseSelectionHook hook;
    app::GlobalHotkey       hotkey;
    QuietManager            manager;
    llm::LlmClient          client;
    app::AppController      controller;
};

/// @return A value that is not @p row's default, of the row's own shape; nothing for the face, whose
///         default is the only family a test can name on every machine.
std::optional<QVariant> movedOffDefault(Row const& row)
{
    // The whitelist is parsed name by name on the way in, so a made-up value has to be a real one.
    if (row.key == "scanWhitelist")
        return QVariant{QStringLiteral("notepad.exe")};
    // A path this machine does not have would start an OCR probe, which is not this suite's business.
    if (row.key == "tesseractExecutable" or row.key == "tesseractDataDirectory")
        return std::nullopt;
    if (row.key == "ocrOnlineProvider") return QVariant{QStringLiteral("ocrspace")};
    if (row.key == "ocrOnlineEndpoint") return QVariant{QStringLiteral("https://example.invalid/parse/image")};

    switch (row.kind) {
        case Kind::Toggle:
            return QVariant{not row.fallback.toggle};
        case Kind::Integer:
            return QVariant{row.fallback.number == row.high ? row.low : row.high};
        case Kind::Choice:
            for (std::string_view const member : row.members)
                if (member != row.fallback.text)
                    return QVariant{QString::fromStdString(std::string{member})};
            return std::nullopt;
        case Kind::Font:
            return std::nullopt;
        case Kind::Text:
            return QVariant{QString::fromStdString(std::string{row.fallback.text} + "x")};
    }
    return std::nullopt;
}

} // namespace

/// A sync carries a key because its row says so, not a hand-written whitelist: with one list the
/// two cannot drift.
TEST(SettingsTable, TheSyncWhitelistIsTheTablesSyncedRows)
{
    std::set<std::string> fromTable;
    for (auto const& row : core::settings::kRows)
        if (row.synced)
            fromTable.insert(std::string{row.key});

    std::set<std::string> fromSync;
    for (auto const& key : core::sync::syncedKeys())
        fromSync.insert(key);

    EXPECT_EQ(fromTable, fromSync);
}

/// The projection hands every row out under its own key. A setting a surface can write but not read
/// back is a setting whose write the reader cannot see.
TEST_F(SettingsTableRig, EveryRowAppearsInTheProjection)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    const QVariantMap projection = rig.controller.settings();
    for (auto const& row : core::settings::kRows)
        EXPECT_TRUE(projection.contains(app::settings::keyOf(row))) << "not in the projection: " << row.key;
}

/// A value written through a row reads back through the same row, and an out-of-range one lands on
/// the same edge whether it was written or hand-edited into the document: a reader can never see a
/// value the writer would refuse.
TEST_F(SettingsTableRig, EveryIntegerRowClampsOnBothSides)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    for (auto const& row : core::settings::kRows) {
        if (row.kind != Kind::Integer)
            continue;
        const QString key = app::settings::keyOf(row);

        // Past the top edge, then past the floor: the row's own clamp answers both.
        ASSERT_TRUE(rig.controller.setSetting(key, row.high + 10)) << row.key;
        EXPECT_EQ(rig.stored(row).toInt(), row.high) << row.key;
        ASSERT_TRUE(rig.controller.setSetting(key, row.low - 10)) << row.key;
        EXPECT_EQ(rig.stored(row).toInt(), row.low) << row.key;

        // The same number, written into the document by hand, reads as the edge too. The level is
        // not this half's: save() re-stamps it from the store, so a hand edit never reaches a read.
        if (row.signal != Signal::Level) {
            rig.store.section().set<int>(row.key, row.high + 10);
            EXPECT_EQ(rig.stored(row).toInt(), row.high) << row.key;
        }
    }
}

/// A Choice row refuses a member it does not list, and a stored one it does not list reads as the
/// default: the page cannot write a value the bubble does not know, and a hand edit cannot either.
TEST_F(SettingsTableRig, ChoiceRowsRefuseWhatTheyDoNotList)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    for (auto const& row : core::settings::kRows) {
        if (row.kind != Kind::Choice)
            continue;
        const QString fallback = QString::fromStdString(std::string{row.fallback.text});
        EXPECT_FALSE(rig.controller.setSetting(app::settings::keyOf(row), QStringLiteral("not-a-member"))) << row.key;
        EXPECT_EQ(rig.stored(row).toString(), fallback) << row.key << " kept a value its row does not list";

        const QString member = QString::fromStdString(std::string{row.members.front()});
        ASSERT_TRUE(rig.controller.setSetting(app::settings::keyOf(row), member)) << row.key;
        EXPECT_EQ(rig.stored(row).toString(), member) << row.key;

        rig.store.section().set<std::string>(row.key, "not-a-member");
        EXPECT_EQ(rig.stored(row).toString(), fallback) << row.key << " read back a value its row does not list";
    }
}

/// The OCR credentials are secrets, not rows: they never reach the projection or the sync
/// whitelist, and the surface only learns whether each slot is filled.
TEST_F(SettingsTableRig, TheOcrKeyIsNotAProjectionRowAndDoesNotSync)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    ASSERT_TRUE(rig.controller.setSetting(QStringLiteral("ocrOnlineProvider"), QStringLiteral("ocrspace")));
    rig.controller.captureDuty()->setOcrCredential(QStringLiteral("apiKey"), QStringLiteral("ocr-test-token"));
    auto const projection = rig.controller.settings();
    EXPECT_FALSE(projection.contains(QStringLiteral("OCR-API-KEY")));
    EXPECT_FALSE(projection.contains(QStringLiteral("OCR-SECRET-KEY")));
    bool apiKeyPresent = false;
    for (auto const& entry : projection.value(QStringLiteral("ocrOnlineCredentials")).toList()) {
        auto const slot = entry.toMap();
        if (slot.value(QStringLiteral("slot")).toString() == QLatin1String("apiKey"))
            apiKeyPresent = slot.value(QStringLiteral("present")).toBool();
    }
    EXPECT_TRUE(apiKeyPresent);

    for (auto const& key : core::sync::syncedKeys()) {
        EXPECT_NE(key, "OCR-API-KEY");
        EXPECT_NE(key, "OCR-SECRET-KEY");
    }
    EXPECT_FALSE(rig.store.document().value("OCR-API-KEY", std::string{}).empty())
        << "the key is stored in the profile, just not projected";
}

/// Clearing the online configuration also removes what each service kept for switching back.
TEST_F(SettingsTableRig, ClearingOnlineOcrConfigurationRemovesTheSavedServiceValues)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    ASSERT_TRUE(rig.controller.setSetting(QStringLiteral("ocrOnlineProvider"), QStringLiteral("ocrspace")));
    rig.controller.captureDuty()->setOcrCredential(QStringLiteral("apiKey"), QStringLiteral("token"));
    ASSERT_TRUE(rig.controller.setSetting(QStringLiteral("ocrOnlineProvider"), QStringLiteral("baidu")));
    ASSERT_TRUE(rig.store.document().contains("OCR-SERVICES"));

    rig.controller.clearOcrOnlineConfiguration();
    EXPECT_FALSE(rig.store.document().contains("OCR-SERVICES"));
    EXPECT_TRUE(rig.store.document().value("OCR-API-KEY", std::string{}).empty());
    EXPECT_TRUE(rig.store.document().value("OCR-SECRET-KEY", std::string{}).empty());
}

/// Restore-defaults covers every row the table marks restorable, and leaves the one it does not.
TEST_F(SettingsTableRig, RestoreDefaultsCoversEveryRestorableRow)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    for (auto const& row : core::settings::kRows) {
        if (not row.restorable) continue;
        const auto moved = movedOffDefault(row);
        if (not moved)
            continue;
        ASSERT_TRUE(rig.controller.setSetting(app::settings::keyOf(row), *moved)) << row.key;
        ASSERT_NE(rig.stored(row), app::settings::defaultValue(row)) << row.key << " did not move at all";
    }
    // The interface language is not the panel's to reset, so it is moved by hand and has to stay.
    ASSERT_TRUE(rig.controller.setSetting(QStringLiteral("uiLanguage"), QStringLiteral("ja")));

    rig.controller.restoreDefaults();

    for (auto const& row : core::settings::kRows) {
        if (not row.restorable) {
            // Only the interface language was moved by hand above; the rest are machine state
            // (the OCR configuration version) and keep whatever they were born with.
            const QVariant expected = row.key == "uiLanguage" ? QVariant{QStringLiteral("ja")} : app::settings::defaultValue(row);
            EXPECT_EQ(rig.stored(row), expected) << row.key << " was restored, and it is not the panel's";
            continue;
        }
        EXPECT_EQ(rig.stored(row), app::settings::defaultValue(row)) << row.key << " did not go back to its default";
    }
}

/// The dev surface's whitelist names rows of the table, not keys of its own invention.
TEST(SettingsTable, TheDevSurfacesSettingsAreRowsOfTheTable)
{
    using lens::core::dev::SettingKey;
    using lens::core::dev::settingKeyName;
    for (auto const key : {SettingKey::Theme, SettingKey::LiquidBlur, SettingKey::FrostedBlur, SettingKey::LiquidRefraction, SettingKey::AnimationsEnabled, SettingKey::GlassQuality})
        EXPECT_NE(core::settings::find(settingKeyName(key)), nullptr)
            << "the dev surface names a setting the table does not: " << std::string{settingKeyName(key)};
}

/// A key the table does not own is refused rather than written somewhere else, which is what keeps a
/// typo in a surface a no-op with a line in the log instead of a second copy of a setting.
TEST_F(SettingsTableRig, AnUnknownKeyIsRefused)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    Rig rig{std::filesystem::path{temporary.path().toStdWString()}};

    EXPECT_FALSE(rig.controller.setSetting(QStringLiteral("frothedBlur"), 10));
    EXPECT_FALSE(rig.controller.setSetting(QString(), 1));
    EXPECT_TRUE(rig.controller.settingRange(QStringLiteral("frothedBlur")).isEmpty());
    EXPECT_EQ(rig.controller.settingRange(QStringLiteral("frostedBlur")).value(QStringLiteral("high")).toInt(), 100);
}
