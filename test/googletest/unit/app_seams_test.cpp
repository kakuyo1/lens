/**
 * @file app_seams_test.cpp
 * @brief The pure halves of the app seams: the notice payload's shape, the Run entry's key and
 *        command, the tray state's priority, the explanation queue's scheduling and staleness,
 *        the model a service carries and whether changing it moves the model, the annual grid's
 *        window, and the settings document's path.
 *
 * Each is the side of a seam that holds no more than Qt Core or the standard library -- which
 * is the difference between an assertion that runs on every change and one a human has to
 * remember. The sides that touch the machine stay human-run cases in lens_gtest_integration
 * (TEST.md section 2): the registry write, and what the live tray icon actually draws.
 */

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <QDate>
#include <QJsonArray>
#include <QStandardPaths>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <gtest/gtest.h>

#include "app/autostart.h"
#include "app/bubble_settings.h"
#include "app/capture_refusal.h"
#include "app/explanation_queue.h"
#include "app/model_catalog.h"
#include "app/notice.h"
#include "app/paths.h"
#include "app/reveal_settings.h"
#include "app/tray_state.h"
#include "app/year_grid.h"
#include "capture/ocr_engine.h"
#include "core/stats_store.h"
#include "support.h"

namespace {

using lens::app::autostartCommand;
using lens::app::clampBubbleDismissSeconds;
using lens::app::clampRevealSpeed;
using lens::app::kAutostartRunKey;
using lens::app::kAutostartValueName;
using lens::app::kBubbleDismissDefaultSeconds;
using lens::app::kBubbleDismissMaxSeconds;
using lens::app::kBubbleDismissMinSeconds;
using lens::app::kNoticeError;
using lens::app::kNoticeInfo;
using lens::app::kRevealSpeedDefault;
using lens::app::kRevealSpeedMax;
using lens::app::kRevealSpeedMin;
using lens::app::noticePayload;
using lens::app::refusalKey;
using lens::app::revealMillisPerCharacter;
using lens::app::settingsPath;
using lens::app::toPath;
using lens::app::CaptureRefusal;
using lens::app::ExplanationQueue;
using lens::app::modelFollowingProvider;
using lens::app::modelsKnownFor;
using lens::app::PendingSelection;
using lens::app::TrayState;
using lens::app::trayIconName;
using lens::app::trayState;
using lens::app::yearGrid;
using lens::core::CacheContext;
using lens::core::DailyUsage;

/// @return The token a runtime status is reported by, or "<none>" when it is not a reason at all.
std::string reasonToken(lens::capture::OcrStatus status)
{
    const auto refusal = lens::app::refusalOf(status);
    return refusal ? lens::app::refusalKey(*refusal) : "<none>";
}

/// @return A word candidate, the shape a scan batch and a word-list row both arrive in.
PendingSelection aWord(QString const& lemma)
{
    PendingSelection selection;
    selection.kind   = QStringLiteral("word");
    selection.text   = lemma;
    selection.lemma  = lemma;
    selection.anchor = QPoint{200, 400};
    return selection;
}

/// @return A selection that is not a word, so the cache and the sense mode do not apply to it.
PendingSelection aSentence(QString const& text)
{
    PendingSelection selection;
    selection.kind    = QStringLiteral("sentence");
    selection.text    = text;
    selection.surface = text;
    return selection;
}

/// @return The context a request goes out under: the explanation language and the sense mode.
CacheContext aContext(char const* language = "en", bool multipleSenses = false)
{
    return {language, multipleSenses};
}

} // namespace

/// The shape is the contract the notice surface is built against, so it is pinned here
/// rather than left to whatever the controller happens to put in the map.
TEST(NoticePayload, CarriesAReasonAndAtMostOneAction)
{
    const QVariantMap notice = noticePayload(QStringLiteral("ubiquitous"),
                                             QString::fromUtf8("这张卡片没有可解释的单词"),
                                             QString::fromUtf8(kNoticeInfo));

    EXPECT_EQ(notice.size(), 5) << "a sixth key would be one no surface reads";
    EXPECT_EQ(notice.value("title").toString(), QStringLiteral("ubiquitous"));
    EXPECT_EQ(notice.value("body").toString(), QString::fromUtf8("这张卡片没有可解释的单词"));
    EXPECT_EQ(notice.value("kind").toString(), QStringLiteral("info"));
    // Most notices are the end of the road: nothing to press, and no word to press it about.
    EXPECT_TRUE(notice.value("action").toString().isEmpty());
    EXPECT_TRUE(notice.value("lemma").toString().isEmpty());
}

/// A notice that does offer one names the word it would act on, because the surface that draws the
/// button does not know what the button means -- the side that raised the notice does.
TEST(NoticePayload, AnOfferedActionNamesWhatItActsOn)
{
    const QVariantMap notice = noticePayload(QStringLiteral("panel"),
                                             QStringLiteral("No explanation is stored for this word."),
                                             QString::fromUtf8(kNoticeInfo),
                                             QStringLiteral("Explain now"),
                                             QStringLiteral("panel"));

    EXPECT_EQ(notice.value("action").toString(), QStringLiteral("Explain now"));
    EXPECT_EQ(notice.value("lemma").toString(), QStringLiteral("panel"));
}

TEST(NoticePayload, TheTwoKindsAreDistinctAndATitleMayBeAbsent)
{
    EXPECT_STRNE(kNoticeInfo, kNoticeError);

    // No title is the contention case: nothing was selected, so there is nothing to name.
    const QVariantMap notice = noticePayload(QString(), QStringLiteral("x"), QString::fromUtf8(kNoticeError));
    EXPECT_TRUE(notice.value("title").toString().isEmpty());
    EXPECT_EQ(notice.value("kind").toString(), QString::fromUtf8(kNoticeError));
}

/// The one key the product writes outside its own document, so the path is worth a case: it is
/// read by nothing in this process and a typo in it fails silently.
TEST(Autostart, TheEntrySitsUnderTheUsersRunKey)
{
    EXPECT_STREQ(kAutostartRunKey, R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)");
    EXPECT_STREQ(kAutostartValueName, "Lens");
}

TEST(Autostart, TheCommandQuotesTheExecutablePath)
{
    // Quoted even without a space, so the form is one thing rather than two cases, and a path
    // that has spaces is never the branch nobody tried. The fixtures carry no drive letter:
    // what is being pinned is the quoting, and a drive path in a source file is what the
    // pre-commit hook's check exists to reject (config/README.md).
    EXPECT_EQ(autostartCommand(R"(Lens\lens.exe)"), R"("Lens\lens.exe")");

    // What the quoting is for: unquoted, Windows would run the first word and hand the rest
    // over as arguments.
    EXPECT_EQ(autostartCommand(R"(Program Files\Lens\lens.exe)"), R"("Program Files\Lens\lens.exe")");
}

/// The tray state is a priority, not a sum: an exhausted budget is the one thing the reader has
/// to act on, so it outranks a request in flight and the capture switch both. Nothing behind
/// Tray's private half could assert it, which is why the rule lives in tray_state.h.
TEST(TrayState, TheExhaustedBudgetOutranksTheRestAndEveryStateNamesItsArt)
{
    EXPECT_EQ(trayState(true, true, true), TrayState::Budget);
    EXPECT_EQ(trayState(true, false, false), TrayState::Budget);
    EXPECT_EQ(trayState(false, true, true), TrayState::Busy);
    EXPECT_EQ(trayState(false, false, true), TrayState::Auto);
    EXPECT_EQ(trayState(false, false, false), TrayState::Off);

    // The name is the middle of a file name under :/icons, so a state that answers with a word
    // no art carries leaves the shell drawing a blank slot rather than saying anything.
    EXPECT_STREQ(trayIconName(TrayState::Budget), "budget");
    EXPECT_STREQ(trayIconName(TrayState::Busy), "busy");
    EXPECT_STREQ(trayIconName(TrayState::Auto), "auto");
    EXPECT_STREQ(trayIconName(TrayState::Off), "off");
}

/// Every reason names a token, and the token is the byte string that crossed this seam before the
/// reason had a type: the QML side and the catalogue are written against these, so a rename here
/// is a change there. The facade reads the token to pick a sentence and nothing else.
TEST(CaptureRefusal, EveryReasonNamesTheTokenTheSurfacesAlreadyRead)
{
    EXPECT_STREQ(refusalKey(CaptureRefusal::BudgetPaused), "budget-paused");
    EXPECT_STREQ(refusalKey(CaptureRefusal::Checking), "checking");
    EXPECT_STREQ(refusalKey(CaptureRefusal::Busy), "busy");
    EXPECT_STREQ(refusalKey(CaptureRefusal::ScreenshotOff), "screenshot-off");
    EXPECT_STREQ(refusalKey(CaptureRefusal::OutsideScreen), "outside-screen");
    EXPECT_STREQ(refusalKey(CaptureRefusal::RegionTooLarge), "too-large");
    EXPECT_STREQ(refusalKey(CaptureRefusal::EmptyText), "empty-text");
    EXPECT_STREQ(refusalKey(CaptureRefusal::InvalidImage), "invalid-image");
    EXPECT_STREQ(refusalKey(CaptureRefusal::LanguageMissing), "english-data-missing");
    EXPECT_STREQ(refusalKey(CaptureRefusal::RuntimeMissing), "runtime-missing");
    EXPECT_STREQ(refusalKey(CaptureRefusal::StartFailed), "start-failed");
    EXPECT_STREQ(refusalKey(CaptureRefusal::TimedOut), "timeout");
    EXPECT_STREQ(refusalKey(CaptureRefusal::Failed), "failed");
}

/// One reason per runtime status, and Ready is the one that is not a reason at all: the gate that
/// used to compare against Ready asks this instead. Both switches name every value and neither has
/// a default arm, so the whole mapping reads in one place -- and what an unclassified status falls
/// through to is a runtime that fell over, not a capture that goes ahead.
TEST(CaptureRefusal, EveryRuntimeStatusNamesItsOwnReasonAndReadyNamesNone)
{
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::Ready), "<none>");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::RuntimeMissing), "runtime-missing");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::LanguageMissing), "english-data-missing");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::StartFailed), "start-failed");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::TimedOut), "timeout");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::Failed), "failed");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::InvalidImage), "invalid-image");
    EXPECT_EQ(reasonToken(lens::capture::OcrStatus::EmptyText), "empty-text");
}

/// Each gate refuses on its own, and the reader hears the highest-ranked one when several hold.
TEST(CaptureRefusal, TheGateNamesEachReasonAndTheHighestRankedWins)
{
    using lens::app::gateRefusal;
    using lens::capture::OcrStatus;
    EXPECT_EQ(gateRefusal({.budgetPaused = true, .screenshotOn = true, .runtime = OcrStatus::Ready}), CaptureRefusal::BudgetPaused);
    EXPECT_EQ(gateRefusal({.probing = true, .busy = true, .screenshotOn = true, .runtime = OcrStatus::Ready}), CaptureRefusal::Checking);
    EXPECT_EQ(gateRefusal({.busy = true, .screenshotOn = true, .runtime = OcrStatus::Ready}), CaptureRefusal::Busy);
    EXPECT_EQ(gateRefusal({.screenshotOn = false, .runtime = OcrStatus::Ready}), CaptureRefusal::ScreenshotOff);
    EXPECT_EQ(gateRefusal({.screenshotOn = true, .runtime = OcrStatus::TimedOut}), CaptureRefusal::TimedOut);
    EXPECT_EQ(gateRefusal({.screenshotOn = true, .runtime = OcrStatus::Ready}), std::nullopt);

    // Every gate shut at once, then opened one by one from the top.
    lens::app::CaptureGate gate{.budgetPaused = true, .probing = true, .busy = true, .screenshotOn = false, .runtime = OcrStatus::RuntimeMissing};
    EXPECT_EQ(gateRefusal(gate), CaptureRefusal::BudgetPaused);
    gate.budgetPaused = false;
    EXPECT_EQ(gateRefusal(gate), CaptureRefusal::Checking);
    gate.probing = false;
    EXPECT_EQ(gateRefusal(gate), CaptureRefusal::Busy);
    gate.busy = false;
    EXPECT_EQ(gateRefusal(gate), CaptureRefusal::ScreenshotOff);
    gate.screenshotOn = true;
    EXPECT_EQ(gateRefusal(gate), CaptureRefusal::RuntimeMissing);
    gate.runtime = OcrStatus::Ready;
    EXPECT_EQ(gateRefusal(gate), std::nullopt);
}

/// @return The grid's entry for @p date, or an empty map when the window does not carry it.
QVariantMap gridDay(const QVariantList& grid, const QString& date)
{
    for (const QVariant& day : grid) {
        if (day.toMap().value("date").toString() == date)
            return day.toMap();
    }
    return {};
}

/// The window is fixed at 365 days ending on the day it is handed, which is the reason the
/// projection left AppController: there the date came from the clock, so every assertion was
/// about whichever day the suite happened to run on.
TEST(YearGrid, IsThreeHundredAndSixtyFiveDaysEndingOnTheDayItIsGiven)
{
    const QVariantList grid = yearGrid(QDate(2026, 3, 1), {});
    ASSERT_EQ(grid.size(), 365);

    // Read back through QDate rather than written out: the first day is 364 days before the
    // last, and that arithmetic is the assertion.
    const QString first = grid.first().toMap().value("date").toString();
    EXPECT_EQ(QDate::fromString(first, QStringLiteral("yyyy-MM-dd")).addDays(364), QDate(2026, 3, 1));
    EXPECT_EQ(grid.last().toMap().value("date").toString(), QStringLiteral("2026-03-01"));

    // A year with nothing in it is 365 squares, not an empty surface.
    EXPECT_EQ(grid.first().toMap().value("pops").toInt(), 0);
    EXPECT_EQ(grid.last().toMap().value("pops").toInt(), 0);
}

TEST(YearGrid, CarriesTheLeapDayWhenTheWindowReachesOne)
{
    // A year back from March 2024 crosses 2024-02-29. The date is what the surface keys the
    // square on, so a day the window skipped is a day the reader cannot be shown.
    const QVariantList grid = yearGrid(QDate(2024, 3, 1), {});
    EXPECT_FALSE(gridDay(grid, QStringLiteral("2024-02-29")).isEmpty());
    EXPECT_EQ(gridDay(grid, QStringLiteral("2024-02-29")).value("pops").toInt(), 0);
}

TEST(YearGrid, SpendsTheTalliesOfTheOneDayItHasAndLeavesTheRestAtZero)
{
    std::map<std::string, DailyUsage> daily;
    daily["2026-02-14"] = DailyUsage{42, 3, 5};
    daily["2026-02-20"] = DailyUsage{1, 0, 1};

    const QVariantList grid = yearGrid(QDate(2026, 3, 1), daily);

    const QVariantMap heaviest = gridDay(grid, QStringLiteral("2026-02-14"));
    EXPECT_EQ(heaviest.value("pops").toInt(), 42);
    EXPECT_EQ(heaviest.value("learned").toInt(), 3);
    EXPECT_EQ(heaviest.value("fresh").toInt(), 5);

    // The days around it are squares of their own, and a day with no record is zero rather than
    // missing: the surface draws the whole window from this list and from nothing else.
    EXPECT_EQ(gridDay(grid, QStringLiteral("2026-02-20")).value("pops").toInt(), 1);
    EXPECT_EQ(gridDay(grid, QStringLiteral("2026-02-15")).value("pops").toInt(), 0);

    // A tally the window does not reach is not in it.
    EXPECT_TRUE(gridDay(grid, QStringLiteral("2026-03-02")).isEmpty());
}

/// Test mode is process-wide, so it is put back: this binary has other suites, and none of them
/// should inherit a redirected profile.
struct ProfileScope {
    ProfileScope()
    {
        QStandardPaths::setTestModeEnabled(true);
    }
    ~ProfileScope()
    {
        QStandardPaths::setTestModeEnabled(false);
    }
};

/// @return @p path's contents, for the carry-over case below.
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The boundary: the document carries the reader's API key and word marks, so it belongs to the
/// profile -- one per account -- and never to the install tree, which every account shares and
/// which may be read-only under Program Files.
TEST(SettingsPath, LivesInTheProfileAndNeverInTheInstallTree)
{
    const ProfileScope          scope;
    const std::filesystem::path target = settingsPath({});

    EXPECT_EQ(target.filename(), "settings.json");
    // Pinned to the platform's profile location, which is the assertion the section is about:
    // the install tree is never the profile location, so a resolver that reached for the
    // executable's own directory fails here.
    EXPECT_EQ(target.parent_path(), toPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));

    // Said the other way round as well, against the tree this binary was built from: there is no
    // QCoreApplication here, so QCoreApplication::applicationDirPath() would answer with an empty
    // string and the comparison would hold for the wrong reason.
    const std::filesystem::path under = target.parent_path().lexically_relative(lens::test::sourceDir());
    EXPECT_TRUE(under.empty() || under.string().starts_with(".."))
        << "the document resolved under the tree it was built from: " << target.string();

    // Documented, and what the caller relies on: the directory is there when it returns.
    EXPECT_TRUE(std::filesystem::is_directory(target.parent_path()));

    std::filesystem::remove_all(target.parent_path());
}

/// The other half of the same rule: settings.local.json is a development source, carried into
/// the profile once so a key already configured keeps working -- and then never again, or a
/// later launch would put the repository's copy back over the reader's own.
TEST(SettingsPath, CarriesTheLegacyDocumentOverOnceAndOnlyOnce)
{
    const ProfileScope          scope;
    const std::filesystem::path profile = settingsPath({});
    std::filesystem::remove(profile);

    const std::filesystem::path legacy = std::filesystem::temp_directory_path() / "lens-legacy-settings.json";
    {
        std::ofstream out(legacy);
        out << R"({"API-KEY":"from-the-repo"})";
    }

    EXPECT_EQ(settingsPath(legacy), profile);
    EXPECT_NE(readFile(profile).find("from-the-repo"), std::string::npos);

    {
        std::ofstream out(profile);
        out << R"({"API-KEY":"the-readers"})";
    }
    settingsPath(legacy);

    const std::string carried = readFile(profile);
    EXPECT_NE(carried.find("the-readers"), std::string::npos);
    EXPECT_EQ(carried.find("from-the-repo"), std::string::npos)
        << "the carry-over ran again over a document the reader has since written";

    std::filesystem::remove(legacy);
    std::filesystem::remove_all(profile.parent_path());
}

/// The queue is what the reader has asked for and not yet been answered. These cases are the
/// scheduling and the staleness rule itself: which request leaves next, which answer still belongs
/// to anything, and when an answer has stopped being the reader's. They used to be reachable only
/// by driving the whole duty through a faked QNetworkReply and a faked event loop, which is what
/// made them the part nobody changed.
TEST(ExplanationQueue, AScanBatchTakesFiveAndLeavesTheRestQueued)
{
    ExplanationQueue              queue;
    std::vector<PendingSelection> candidates;
    for (int i = 0; i < 7; ++i)
        candidates.push_back(aWord(QStringLiteral("w%1").arg(i)));
    queue.addScan(QVector<PendingSelection>{candidates.begin(), candidates.end()});

    const auto first = queue.take(true, aContext());
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->fromScan);
    ASSERT_EQ(first->items.size(), 5) << "one request buys five words, not seven";
    EXPECT_EQ(first->items.front().lemma, QStringLiteral("w0"));

    // Nothing leaves while the answer to this batch is still being waited for: the caller has
    // not disposed of it yet, and a second request in flight would have nowhere to land.
    EXPECT_FALSE(queue.take(true, aContext()).has_value());
    queue.complete();

    const auto second = queue.take(true, aContext());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->items.size(), 2) << "the two the cap left behind";
    EXPECT_EQ(second->items.back().lemma, QStringLiteral("w6"));
}

/// The reader pointing at a word outranks the word list filling itself in beside it: the scan
/// batch is not lost, it is simply next.
TEST(ExplanationQueue, ARequestTheReaderMadeOvertakesQueuedScanWork)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("scanned"))});
    queue.request(aWord(QStringLiteral("asked")));

    const auto asked = queue.take(true, aContext());
    ASSERT_TRUE(asked.has_value());
    EXPECT_FALSE(asked->fromScan);
    ASSERT_EQ(asked->items.size(), 1);
    EXPECT_EQ(asked->items.front().lemma, QStringLiteral("asked"));

    queue.complete();
    const auto scanned = queue.take(true, aContext());
    ASSERT_TRUE(scanned.has_value());
    EXPECT_TRUE(scanned->fromScan);
    EXPECT_EQ(scanned->items.front().lemma, QStringLiteral("scanned"));
}

/// The budget is asked at the moment something is about to leave, not when it was queued: a budget
/// that runs out while a request waits does not throw that request away, it holds it back with the
/// word list's own work, and the next moment the budget allows drains both.
TEST(ExplanationQueue, AnExhaustedBudgetDrainsNothingAndKeepsWhatIsAlreadyQueued)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("scanned"))});
    queue.request(aWord(QStringLiteral("asked")));

    EXPECT_FALSE(queue.take(false, aContext()).has_value()) << "nothing leaves with no budget";

    const auto asked = queue.take(true, aContext());
    ASSERT_TRUE(asked.has_value());
    EXPECT_FALSE(asked->fromScan);
    queue.complete();

    const auto scanned = queue.take(true, aContext());
    ASSERT_TRUE(scanned.has_value()) << "and the held-back scan work is still there";
    EXPECT_EQ(scanned->items.front().lemma, QStringLiteral("scanned"));
}

/// A word deleted locally is gone from both halves of the queue. A deletion reaches a word that is
/// waiting, and it is compared without case: the reader deleted the word, not a spelling of it.
TEST(ExplanationQueue, AWordDeletedBeforeItIsSentLeavesTheQueueAndTheHeldRequest)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("kept")), aWord(QStringLiteral("Deleted"))});
    queue.request(aWord(QStringLiteral("gone")));

    queue.invalidate(QStringLiteral("gone"));
    queue.invalidate(QStringLiteral("deleted"));

    const auto batch = queue.take(true, aContext());
    ASSERT_TRUE(batch.has_value());
    EXPECT_TRUE(batch->fromScan) << "the held request was the one deleted";
    ASSERT_EQ(batch->items.size(), 1);
    EXPECT_EQ(batch->items.front().lemma, QStringLiteral("kept"));
}

/// A batch of five is answered as five results, and the answer says which word each one is for by
/// naming it -- the order they come back in is the model's, not the request's.
TEST(ExplanationQueue, AnAnswerIsPairedWithItsItemByLemmaAndShownAboutTheItemTheFirstOneNames)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("bank")), aWord(QStringLiteral("river"))});
    ASSERT_TRUE(queue.take(true, aContext()).has_value());

    const auto settlement = queue.settled({QStringLiteral("river"), QStringLiteral("bank")}, aContext());

    ASSERT_EQ(settlement.resultFor.size(), 2) << "one entry per item of the batch, in its order";
    EXPECT_EQ(settlement.resultFor[0], 1) << "bank is the second answer";
    EXPECT_EQ(settlement.resultFor[1], 0) << "river is the first";

    // The answer is stored under the context its request went out with and not the one in force
    // now: those can differ, and telling them apart is what the staleness rule below is about.
    ASSERT_TRUE(settlement.batch.fromScan);
    EXPECT_EQ(settlement.batch.context.language, "en");
    EXPECT_EQ(settlement.batch.items.size(), settlement.resultFor.size());

    // The scan bubble goes up over the item the first answer names rather than over the batch's
    // first, so the word the reader is being shown is the one at the top of the card.
    ASSERT_TRUE(settlement.subject.has_value());
    EXPECT_EQ(settlement.subject->lemma, QStringLiteral("river"));
    EXPECT_EQ(settlement.subject->anchor, QPoint(200, 400));
}

/// Deleting a word while its answer is on the way does two things and they are not the same thing:
/// the answer is never stored against a word that no longer exists, and it is not put on screen.
TEST(ExplanationQueue, AWordDeletedWhileItsAnswerWasComingIsNeitherStoredNorShown)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("bank"))});
    ASSERT_TRUE(queue.take(true, aContext()).has_value());

    queue.invalidate(QStringLiteral("bank"));
    const auto settlement = queue.settled({QStringLiteral("bank")}, aContext());

    EXPECT_EQ(settlement.resultFor.front(), -1) << "nothing to store it against";
    EXPECT_FALSE(settlement.subject.has_value()) << "and nothing to show it about";
}

/// The one that is easy to get wrong: the reader moving on takes the answer off the screen without
/// making it worthless. It was paid for, and the word is still a word -- it is stored, and only
/// the bubble is withheld.
TEST(ExplanationQueue, ANewSelectionLeavesAnAnswerOnItsWayUnshownButStillStored)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("bank"))});
    ASSERT_TRUE(queue.take(true, aContext()).has_value());

    queue.selectionChanged();
    const auto settlement = queue.settled({QStringLiteral("bank")}, aContext());

    EXPECT_EQ(settlement.resultFor.front(), 0) << "the answer is still this word's";
    EXPECT_FALSE(settlement.subject.has_value()) << "the reader is looking at something else now";
}

/// The two ways the settings can move under a request in flight. Both leave the answer just as
/// worth storing and just as wrong to show: it is in the language, and the sense mode, that the
/// reader asked in -- not the ones they are in now.
TEST(ExplanationQueue, AChangedLanguageOrSenseModeLeavesTheAnswerUnshown)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("bank"))});
    ASSERT_TRUE(queue.take(true, aContext("en", true)).has_value());

    EXPECT_TRUE(queue.settled({QStringLiteral("bank")}, aContext("en", true)).subject.has_value());
    EXPECT_FALSE(queue.settled({QStringLiteral("bank")}, aContext("en", false)).subject.has_value());
    EXPECT_FALSE(queue.settled({QStringLiteral("bank")}, aContext("zh", true)).subject.has_value());
}

/// The sense mode is a word's, not a sentence's: a sentence has nothing to split into senses, so
/// the setting that is on for the words around it is not what the sentence was asked under.
TEST(ExplanationQueue, ASentenceIsAskedWithoutTheSenseMode)
{
    ExplanationQueue queue;
    queue.request(aSentence(QStringLiteral("A whole sentence.")));

    const auto batch = queue.take(true, aContext("en", true));
    ASSERT_TRUE(batch.has_value());
    EXPECT_FALSE(batch->context.multipleSenses);
    EXPECT_EQ(batch->context.language, "en");
}

/// A scan that fails is not worth a card: the reader did not ask for it and cannot act on it. A
/// request they made themselves is, unless they have since moved on.
TEST(ExplanationQueue, ARefusalIsReportedOnlyForARequestTheReaderStillWants)
{
    ExplanationQueue queue;
    queue.addScan({aWord(QStringLiteral("bank"))});
    ASSERT_TRUE(queue.take(true, aContext()).has_value());
    EXPECT_FALSE(queue.refusalSubject().has_value());
    queue.complete();

    queue.request(aWord(QStringLiteral("asked")));
    ASSERT_TRUE(queue.take(true, aContext()).has_value());
    ASSERT_TRUE(queue.refusalSubject().has_value());
    EXPECT_EQ(queue.refusalSubject()->lemma, QStringLiteral("asked"));

    queue.selectionChanged();
    EXPECT_FALSE(queue.refusalSubject().has_value());
}

/// What a service is known to carry, and whether changing service moves the model with it. Both
/// are ADR-0017's rule, kept by ADR-0020 only for the change of service -- and until this lived
/// here they were reachable only by driving a real duty through a faked network round trip.
TEST(ModelCatalog, AModelOfAnotherServiceDoesNotSurviveTheSwitchToThisOne)
{
    // The name that reaches the wire is the one the service being switched to answers 400 about,
    // and the reader changed the service rather than the model (docs/adr/0017).
    const auto moved = modelFollowingProvider(true, {QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")}, QStringLiteral("gpt-4.1-mini"));
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(*moved, QStringLiteral("deepseek-flash")) << "the most common one is the first";
}

/// Switching back and forth must not lose a choice that was still valid: a model this service does
/// carry is left exactly as the reader set it.
TEST(ModelCatalog, AModelTheServiceDoesCarryIsLeftWhereItIs)
{
    const QStringList carried{QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")};
    EXPECT_FALSE(modelFollowingProvider(true, carried, QStringLiteral("deepseek-v4-pro")).has_value());
}

/// Re-picking the service already in force is a tap on the dropdown row, not a change. With one
/// list shared by every service (docs/adr/0020) that tap would otherwise replace a valid model with
/// whichever id the source happens to list first.
TEST(ModelCatalog, RePickingTheServiceAlreadyInForceIsNotAChange)
{
    EXPECT_FALSE(modelFollowingProvider(false, {QStringLiteral("first"), QStringLiteral("second")}, QStringLiteral("mine")).has_value());
}

/// A service that names no model at all -- a custom endpoint, or one the source has never listed --
/// has nothing to offer, so the model the reader is on stays.
TEST(ModelCatalog, AServiceThatNamesNoModelLeavesTheOneInForceAlone)
{
    EXPECT_FALSE(modelFollowingProvider(true, {}, QStringLiteral("deepseek-flash")).has_value());
}

/// The list belongs to the reader: what the shared source listed for a service last time wins over
/// the catalog's own seed, which is only for a machine that has never reached the source.
TEST(ModelCatalog, WhatTheSourceListedWinsOverTheCatalogsSeed)
{
    const QStringList served{QStringLiteral("from-the-source")};
    const QJsonArray  seeded{QStringLiteral("from-the-catalog")};
    EXPECT_EQ(modelsKnownFor(served, seeded), served);
}

/// A machine that has never reached the source reads the seed, and only the ids in it that could
/// be sent: the catalog is a hand-written file, and an entry that is not a name -- or is an empty
/// one -- reaches the field as a blank row the reader cannot use and cannot explain.
TEST(ModelCatalog, AMachineThatNeverReachedTheSourceReadsTheSeedAndDropsWhatIsNotAName)
{
    const QJsonArray seeded{QStringLiteral("deepseek-flash"), QJsonValue(QString{}), QJsonValue(3), QStringLiteral("deepseek-v4-pro")};
    EXPECT_EQ(modelsKnownFor({}, seeded), QStringList({QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")}));
}

/// The display time starts at five seconds, the timing the bubble has always had, and the setting
/// takes whole seconds from one to thirty: a value typed past either edge, or hand-written into the
/// document, lands on the nearer edge rather than being refused.
TEST(BubbleSettings, DisplayTimeStartsAtFiveSecondsAndClampsToOneToThirty)
{
    EXPECT_EQ(clampBubbleDismissSeconds(kBubbleDismissDefaultSeconds), 5);
    EXPECT_EQ(clampBubbleDismissSeconds(12), 12);
    EXPECT_EQ(clampBubbleDismissSeconds(kBubbleDismissMinSeconds), 1);
    EXPECT_EQ(clampBubbleDismissSeconds(0), 1);
    EXPECT_EQ(clampBubbleDismissSeconds(-7), 1);
    EXPECT_EQ(clampBubbleDismissSeconds(kBubbleDismissMaxSeconds), 30);
    EXPECT_EQ(clampBubbleDismissSeconds(31), 30);
}

/// The typing pace is a level from one to five, slowest first, and the level maps to the
/// milliseconds the reveal is written at. The default has to stay the pace the reveal shipped with,
/// because a reader who never opens the setting should see no change.
TEST(RevealSettings, TheSpeedClampsAndMapsToThePaceTheRevealTakes)
{
    EXPECT_EQ(revealMillisPerCharacter(kRevealSpeedDefault), 35) << "the default pace moved";

    EXPECT_EQ(revealMillisPerCharacter(kRevealSpeedMin), 80);
    EXPECT_EQ(revealMillisPerCharacter(kRevealSpeedMax), 15);

    // Out of range, either way, lands on the nearer edge rather than indexing past the table.
    EXPECT_EQ(clampRevealSpeed(0), kRevealSpeedMin);
    EXPECT_EQ(clampRevealSpeed(-3), kRevealSpeedMin);
    EXPECT_EQ(clampRevealSpeed(kRevealSpeedMax + 1), kRevealSpeedMax);
    EXPECT_EQ(revealMillisPerCharacter(0), 80);
    EXPECT_EQ(revealMillisPerCharacter(99), 15);

    // Slower is a longer wait all the way up, so the slider reads as one direction and not two.
    for (int speed = kRevealSpeedMin; speed < kRevealSpeedMax; ++speed)
        EXPECT_GT(revealMillisPerCharacter(speed), revealMillisPerCharacter(speed + 1)) << "at speed " << speed;
}
