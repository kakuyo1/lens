/**
 * @file tst_pet_controller.cpp
 * @brief The pet controller without a window: events move the state, the one clock moves the frame, and the
 *        worn accessories and the size are stored (PHASE3 3.5, 3.7).
 *
 * Guiless and window-free: no window is created and nothing is drawn, so the run touches nothing on the desktop.
 * Its settings document lives in a scratch directory.
 */

#include <filesystem>
#include <fstream>
#include <optional>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <nlohmann/json.hpp>

#include "app/pet/pet_controller.h"
#include "app/pet/pet_store.h"
#include "core/known_store.h"

namespace {

using lens::app::pet::Outfit;
using lens::app::pet::PetController;
using lens::app::pet::PetStore;
using lens::core::KnownStore;
using lens::core::pet::PetEvent;

/// The controller and the store it reads, built the way main() builds them. The reader's pets folder sits beside the
/// settings document, as in main(); a test that wants installed packages creates it there.
struct Rig {
    KnownStore    store;
    PetStore      petStore;
    PetController controller;

    Rig(std::filesystem::path const& settings, std::filesystem::path const& shipped)
        : store(KnownStore::load(settings)), petStore(store), controller(shipped, settings.parent_path() / "installed", petStore)
    {
    }
};

/// @return The ids the picker offers, in the order it lists them.
QStringList idsOf(PetController const& controller)
{
    auto ids = QStringList{};
    for (auto const& pet : controller.pets())
        ids.append(pet.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

std::filesystem::path shippedRoot()
{
    return std::filesystem::path{LENS_DATA_DIR} / "pet";
}

std::filesystem::path settingsIn(QTemporaryDir const& dir)
{
    return std::filesystem::path{dir.path().toStdWString()} / "settings.json";
}

nlohmann::json readJson(std::filesystem::path const& file);
void           writeJson(std::filesystem::path const& file, nlohmann::json const& document);

/// Two pets under `root`: the shipped dog, and a copy of it whose catalogue has no hat, so a switch has one to drop.
void makeTwoPets(std::filesystem::path const& root)
{
    auto const shipped = shippedRoot() / "dog";
    std::filesystem::create_directories(root);
    std::filesystem::copy(shipped, root / "dog", std::filesystem::copy_options::recursive);
    std::filesystem::copy(shipped, root / "hatless", std::filesystem::copy_options::recursive);

    // The copy takes the folder's name as its id, since a pet's id must be its folder's name.
    auto const petFile = root / "hatless" / "pet.json";
    auto       pet     = readJson(petFile);
    pet["id"]          = "hatless";
    writeJson(petFile, pet);

    auto const file = root / "hatless" / "accessories.json";
    auto       in   = std::ifstream{file};
    auto       doc  = nlohmann::json::parse(in);
    auto       kept = nlohmann::json::array();
    for (auto const& entry : doc.at("accessories")) {
        if (entry.at("id") != "hat") kept.push_back(entry);
    }
    doc["accessories"] = kept;
    auto out           = std::ofstream{file};
    out << doc.dump(2);
}

void writeJson(std::filesystem::path const& file, nlohmann::json const& document)
{
    auto out = std::ofstream{file};
    out << document.dump(2);
}

nlohmann::json readJson(std::filesystem::path const& file)
{
    auto in = std::ifstream{file};
    return nlohmann::json::parse(in);
}

/// @return The action names of one pet's animations.json. The blink is an expression entry, so it is not one.
QStringList actionsIn(nlohmann::json const& animations)
{
    auto names = QStringList{};
    for (auto const& entry : animations.at("actions").items()) {
        if (entry.key() != "blink") names.append(QString::fromStdString(entry.key()));
    }
    return names;
}

QStringList sortedCopy(QStringList list)
{
    list.sort();
    return list;
}

} // namespace

class TstPetController : public QObject {
    Q_OBJECT

private slots:
    void eventsDriveTheAction();
    void theClockAdvancesFramesOnlyWhileRunning();
    void wornAccessoriesFollowToggles();
    void sizeIsStoredAndAnnounced();
    void refusedDataLeavesThePetOff();
    void previewShowsAnActionAndTheMachineKeepsItsOwn();
    void previewRunsTheClockOnlyWhileOpen();
    void aOneShotPreviewGoesBackToTheMachine();
    void accessoryCatalogueAndWornSlotsAreExposed();
    void restoresOnlyAccessoriesInTheirOwnSlot();
    void passthroughDefaultsOffAndIsStored();
    void choosingAPetLoadsItAndDropsWhatItLacks();
    void everyShippedPetLoadsWhole();
    void wornAccessoriesFollowAPetSwitch();
    void aRefusedRealPetKeepsThePetOn();
    void anInstalledPackageWaitsForItsConfirmation();
    void aGonePackageFallsBackToTheShippedDefault();
    void anUnconfirmedChoiceStartsOnTheDefault();
};

void TstPetController::eventsDriveTheAction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));

    rig.controller.dragStart();
    QCOMPARE(rig.controller.action(), QStringLiteral("pickup"));

    rig.controller.dragEnd();
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));

    rig.controller.handle(PetEvent::ExplanationShown);
    QCOMPARE(rig.controller.action(), QStringLiteral("study"));

    rig.controller.handle(PetEvent::ExplanationHidden);
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));

    rig.controller.handle(PetEvent::KnownMarked);
    QCOMPARE(rig.controller.action(), QStringLiteral("celebrate"));
}

void TstPetController::theClockAdvancesFramesOnlyWhileRunning()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    // Study loops at 4 fps, so a frame changes within a quarter of a second.
    rig.controller.handle(PetEvent::ExplanationShown);
    QSignalSpy frames(&rig.controller, &PetController::frameChanged);

    QTest::qWait(300);
    QCOMPARE(frames.count(), 0);

    rig.controller.setRunning(true);
    QVERIFY(frames.wait(1000));
    rig.controller.setRunning(false);

    QVERIFY(rig.controller.frameIndex() >= 0);
    QVERIFY(rig.controller.frameIndex() < 6);
}

void TstPetController::wornAccessoriesFollowToggles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    rig.controller.toggleAccessory(QStringLiteral("hat"));
    QCOMPARE(rig.controller.wornAccessories().size(), 1);
    QVERIFY(rig.petStore.outfit().head == std::optional<std::string>{"hat"});

    auto const worn = rig.controller.wornAccessories().first().toMap();
    QVERIFY(worn.value("asset").toString().startsWith("file:///"));
    QCOMPARE(worn.value("zIndex").toInt(), 5);

    rig.controller.toggleAccessory(QStringLiteral("hat"));
    QCOMPARE(rig.controller.wornAccessories().size(), 0);
    QVERIFY(!rig.petStore.outfit().head);
}

void TstPetController::sizeIsStoredAndAnnounced()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    QCOMPARE(rig.controller.scale(), 3);

    QSignalSpy changed(&rig.controller, &PetController::settingsChanged);
    rig.controller.setScale(9);
    QCOMPARE(rig.controller.scale(), rig.controller.maxScale());
    QCOMPARE(changed.count(), 1);

    rig.controller.setScale(2);
    QCOMPARE(rig.controller.scale(), 2);
}

void TstPetController::refusedDataLeavesThePetOff()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), std::filesystem::path{dir.path().toStdWString()} / "no-pets"};

    QVERIFY(!rig.controller.available());
    QVERIFY(!rig.controller.enabled());

    rig.controller.handle(PetEvent::Click);
    rig.controller.setRunning(true);
    QVERIFY(rig.controller.action().isEmpty());
    QCOMPARE(rig.controller.wornAccessories().size(), 0);
}

void TstPetController::previewShowsAnActionAndTheMachineKeepsItsOwn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    rig.controller.handle(PetEvent::ExplanationShown);
    QCOMPARE(rig.controller.action(), QStringLiteral("study"));

    rig.controller.preview(QStringLiteral("celebrate"));
    QCOMPARE(rig.controller.action(), QStringLiteral("celebrate"));

    rig.controller.preview(QStringLiteral("no_such_action"));
    QCOMPARE(rig.controller.action(), QStringLiteral("celebrate"));

    // Ending the preview hands the dog back to the machine, which was reading the whole time.
    rig.controller.setPreviewing(false);
    QCOMPARE(rig.controller.action(), QStringLiteral("study"));
}

void TstPetController::previewRunsTheClockOnlyWhileOpen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    rig.controller.preview(QStringLiteral("celebrate"));
    QSignalSpy frames(&rig.controller, &PetController::frameChanged);

    rig.controller.setPreviewing(true);
    QVERIFY(frames.wait(1000));

    rig.controller.setPreviewing(false);
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));
}

void TstPetController::aOneShotPreviewGoesBackToTheMachine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    // Celebrate plays eight frames at 10 fps, so it has run through within about a second.
    rig.controller.preview(QStringLiteral("celebrate"));
    rig.controller.setPreviewing(true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.controller.action(), QStringLiteral("idle"), 2000);
    rig.controller.setPreviewing(false);
}

void TstPetController::accessoryCatalogueAndWornSlotsAreExposed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    QCOMPARE(rig.controller.accessories().size(), 7);
    QCOMPARE(rig.controller.actions().size(), 11);
    QCOMPARE(rig.controller.actions().first(), QStringLiteral("idle"));

    QSignalSpy outfit(&rig.controller, &PetController::outfitChanged);
    rig.controller.toggleAccessory(QStringLiteral("glasses"));
    QCOMPARE(outfit.count(), 1);
    QCOMPARE(rig.controller.wornIn().value("face").toString(), QStringLiteral("glasses"));
    QCOMPARE(rig.controller.wornIn().value("head").toString(), QString{});
}

void TstPetController::restoresOnlyAccessoriesInTheirOwnSlot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    // Glasses saved in the head slot is bad data: it must not be worn there, and the face slot's glasses still is.
    rig.petStore.setOutfit(Outfit{std::string{"glasses"}, std::string{"glasses"}, std::nullopt});
    PetController restored{shippedRoot(), settingsIn(dir).parent_path() / "installed", rig.petStore};

    QVERIFY(restored.wornIn().value("head").toString().isEmpty());
    QCOMPARE(restored.wornIn().value("face").toString(), QStringLiteral("glasses"));
}

void TstPetController::passthroughDefaultsOffAndIsStored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    QVERIFY(!rig.controller.passthrough());
    rig.controller.setPassthrough(true);
    QVERIFY(rig.controller.passthrough());
    QVERIFY(rig.petStore.passthrough());
}

void TstPetController::choosingAPetLoadsItAndDropsWhatItLacks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto const root = std::filesystem::path{dir.path().toStdWString()} / "pets";
    makeTwoPets(root);
    Rig rig{settingsIn(dir), root};

    QCOMPARE(idsOf(rig.controller), (QStringList{QStringLiteral("dog"), QStringLiteral("hatless")}));
    QCOMPARE(rig.controller.pet(), QStringLiteral("dog"));

    rig.controller.toggleAccessory(QStringLiteral("hat"));
    rig.controller.setPet(QStringLiteral("hatless"));
    QCOMPARE(rig.controller.pet(), QStringLiteral("hatless"));
    QVERIFY(rig.controller.available());
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));
    QVERIFY(rig.controller.wornIn().value("head").toString().isEmpty());
    QVERIFY(!rig.petStore.outfit().head);

    // The hat was dropped, not hidden: choosing the dog again does not bring it back.
    rig.controller.setPet(QStringLiteral("dog"));
    QVERIFY(rig.controller.wornIn().value("head").toString().isEmpty());

    // A pet that cannot be loaded is refused: the reader keeps the pet they had, and the bad id is not saved.
    rig.controller.setPet(QStringLiteral("missing"));
    QVERIFY(rig.controller.available());
    QCOMPARE(rig.controller.pet(), QStringLiteral("dog"));
    QCOMPARE(QString::fromStdString(rig.petStore.pet()), QStringLiteral("dog"));
}

void TstPetController::everyShippedPetLoadsWhole()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    for (auto const& id : {QStringLiteral("mochi"), QStringLiteral("owl"), QStringLiteral("cat")}) {
        auto const folder     = shippedRoot() / id.toStdString();
        auto const animations = readJson(folder / "animations.json");

        rig.controller.setPet(id);
        QVERIFY(rig.controller.available());
        QCOMPARE(rig.controller.pet(), id);
        QCOMPARE(QString::fromStdString(rig.petStore.pet()), id);

        // Every action the file names is listed once; the role actions come first, then the extras of the idle pool.
        auto const names = rig.controller.actions();
        QCOMPARE(sortedCopy(names), sortedCopy(actionsIn(animations)));

        auto roles = QStringList{};
        for (auto const& entry : animations.at("roles").items()) {
            roles.append(QString::fromStdString(entry.value().get<std::string>()));
        }
        roles.removeDuplicates();
        QCOMPARE(sortedCopy(names.mid(0, roles.size())), sortedCopy(roles));
        for (auto const& extra : names.mid(roles.size())) {
            QVERIFY(!roles.contains(extra));
        }

        // Every action shows its own sheets, from this pet's folder, and its anchors sit on whole cells.
        for (auto const& action : names) {
            rig.controller.preview(action);
            QCOMPARE(rig.controller.action(), action);

            auto const sheets = rig.controller.sheets();
            QVERIFY(sheets.value("body").toString().endsWith(QStringLiteral("/%1/sprites/body/%2.png").arg(id, action)));
            QVERIFY(sheets.value("expression").toString().endsWith(QStringLiteral("/sprites/expressions/eyes.png")));
            QCOMPARE(!sheets.value("effect").toString().isEmpty(), animations.at("actions").at(action.toStdString()).contains("effect"));

            auto const anchor = rig.controller.anchors();
            for (auto const& point : {anchor.value("body").toPoint(), anchor.value("face").toPoint()}) {
                QCOMPARE(point.x() % 3, 0);
                QCOMPARE(point.y() % 3, 0);
            }
            // The expression sheet is a row of nine cells.
            QVERIFY(rig.controller.expression() >= 0 and rig.controller.expression() < 9);
        }
        rig.controller.setPreviewing(false);

        // The catalogue the settings page shows is the file's, and every pet carries nine accessories.
        auto catalogue = QStringList{};
        for (auto const& entry : rig.controller.accessories()) {
            catalogue.append(entry.toMap().value("id").toString());
        }
        auto const accessoryFile = readJson(folder / "accessories.json");
        auto       fileIds       = QStringList{};
        for (auto const& entry : accessoryFile.at("accessories"))
            fileIds.append(QString::fromStdString(entry.at("id").get<std::string>()));
        QCOMPARE(catalogue.size(), 9);
        QCOMPARE(sortedCopy(catalogue), sortedCopy(fileIds));
    }
}

void TstPetController::wornAccessoriesFollowAPetSwitch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedRoot()};

    // The dog's hat is not in mochi's catalogue, so it comes off; the glasses are, so they stay.
    rig.controller.toggleAccessory(QStringLiteral("hat"));
    rig.controller.toggleAccessory(QStringLiteral("glasses"));
    rig.controller.setPet(QStringLiteral("mochi"));
    QVERIFY(rig.controller.wornIn().value("head").toString().isEmpty());
    QCOMPARE(rig.controller.wornIn().value("face").toString(), QStringLiteral("glasses"));
    QVERIFY(!rig.petStore.outfit().head);
    QVERIFY(rig.petStore.outfit().face == std::optional<std::string>{"glasses"});

    // Owl's glasses are reading_glasses, so the glasses come off in the face slot too.
    rig.controller.setPet(QStringLiteral("owl"));
    QVERIFY(rig.controller.wornIn().value("face").toString().isEmpty());
    QVERIFY(!rig.petStore.outfit().face);
}

void TstPetController::aRefusedRealPetKeepsThePetOn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto const root = std::filesystem::path{dir.path().toStdWString()} / "pets";
    std::filesystem::create_directories(root);
    std::filesystem::copy(shippedRoot() / "cat", root / "cat", std::filesystem::copy_options::recursive);
    std::filesystem::copy(shippedRoot() / "cat", root / "broken", std::filesystem::copy_options::recursive);
    {
        // The folder has a pet.json, so the picker lists it, but its animations are not a pet's.
        auto out = std::ofstream{root / "broken" / "animations.json"};
        out << "{}";
    }
    Rig rig{settingsIn(dir), root};

    // The refused folder is not offered at all, so choosing it is ignored and the pet on stays on.
    QCOMPARE(idsOf(rig.controller), (QStringList{QStringLiteral("cat")}));
    rig.controller.setPet(QStringLiteral("cat"));
    QVERIFY(rig.controller.available());

    rig.controller.setPet(QStringLiteral("broken"));
    QVERIFY(rig.controller.available());
    QCOMPARE(rig.controller.pet(), QStringLiteral("cat"));
    QCOMPARE(QString::fromStdString(rig.petStore.pet()), QStringLiteral("cat"));
}

void TstPetController::anInstalledPackageWaitsForItsConfirmation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig        rig{settingsIn(dir), shippedRoot()};
    auto const installed = settingsIn(dir).parent_path() / "installed";
    std::filesystem::create_directories(installed);
    std::filesystem::copy(shippedRoot() / "dog", installed / "lounge", std::filesystem::copy_options::recursive);
    auto doc  = readJson(installed / "lounge" / "pet.json");
    doc["id"] = "lounge";
    writeJson(installed / "lounge" / "pet.json", doc);
    rig.controller.refresh();
    QVERIFY(idsOf(rig.controller).contains(QStringLiteral("lounge")));

    QSignalSpy asked(&rig.controller, &PetController::confirmationRequested);

    // The first pick is asked about, and the pet does not switch until the reader confirms.
    rig.controller.setPet(QStringLiteral("lounge"));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(rig.controller.pet(), QStringLiteral("dog"));

    rig.controller.confirmPet(QStringLiteral("lounge"));
    QCOMPARE(rig.controller.pet(), QStringLiteral("lounge"));
    QVERIFY(rig.controller.available());

    // Once confirmed, it is not asked about again, even after the reader picks another pet and comes back.
    rig.controller.setPet(QStringLiteral("dog"));
    rig.controller.setPet(QStringLiteral("lounge"));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(rig.controller.pet(), QStringLiteral("lounge"));
}

void TstPetController::aGonePackageFallsBackToTheShippedDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto const installed = settingsIn(dir).parent_path() / "installed";
    std::filesystem::create_directories(installed);
    std::filesystem::copy(shippedRoot() / "dog", installed / "lounge", std::filesystem::copy_options::recursive);
    auto doc  = readJson(installed / "lounge" / "pet.json");
    doc["id"] = "lounge";
    writeJson(installed / "lounge" / "pet.json", doc);
    {
        auto settings = Rig{settingsIn(dir), shippedRoot()};
        settings.petStore.confirm("lounge");
        settings.petStore.setPet("lounge");
    }
    Rig rig{settingsIn(dir), shippedRoot()};
    QCOMPARE(rig.controller.pet(), QStringLiteral("lounge"));

    QSignalSpy notice(&rig.controller, &PetController::fellBack);
    std::filesystem::remove_all(installed / "lounge");
    rig.controller.refresh();

    // The reader's choice gives way to the shipped default, and the reader is told why.
    QCOMPARE(rig.controller.pet(), QStringLiteral("dog"));
    QVERIFY(rig.controller.available());
    QCOMPARE(QString::fromStdString(rig.petStore.pet()), QStringLiteral("dog"));
    QTRY_COMPARE(notice.count(), 1);
}

void TstPetController::anUnconfirmedChoiceStartsOnTheDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto const installed = settingsIn(dir).parent_path() / "installed";
    std::filesystem::create_directories(installed);
    std::filesystem::copy(shippedRoot() / "dog", installed / "lounge", std::filesystem::copy_options::recursive);
    auto doc  = readJson(installed / "lounge" / "pet.json");
    doc["id"] = "lounge";
    writeJson(installed / "lounge" / "pet.json", doc);
    {
        // A choice saved without the reader's confirmation, which only a hand-edited settings file could hold.
        auto settings = Rig{settingsIn(dir), shippedRoot()};
        settings.petStore.setPet("lounge");
    }
    Rig rig{settingsIn(dir), shippedRoot()};

    QCOMPARE(rig.controller.pet(), QStringLiteral("dog"));
    QVERIFY(rig.controller.available());
}

QTEST_GUILESS_MAIN(TstPetController)
#include "tst_pet_controller.moc"
