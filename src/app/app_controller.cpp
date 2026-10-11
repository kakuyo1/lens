/**
 * @file app_controller.cpp
 * @brief QML facade that wires the application duties to the surfaces.
 */

#include "app_controller.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QQmlEngine>
#include <QJSEngine>

#include <utility>

#include "autostart.h"
#include "notice.h"
#include "update/update_client.h"
#include "util/log.h"
#include "year_grid.h"

namespace lens::app {
namespace {

/// @return The trigger key the reader chose, or the default one when nothing usable is stored.
QKeySequence storedHotkey(StorageDuty& storage)
{
    const QString text = QString::fromStdString(storage.section().value<std::string>("ocrHotkey", {})).trimmed();
    if (text.isEmpty()) return GlobalHotkey::defaultKeys();
    const QKeySequence parsed = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (parsed.isEmpty() or not GlobalHotkey::isUsable(parsed)) {
        LENS_WARN("ignored the stored trigger key '{}'", text.toStdString());
        return GlobalHotkey::defaultKeys();
    }
    return parsed;
}

}

AppController::AppController(core::KnownStore& store,
                             llm::LlmClient& llm,
                             MouseSelectionHook& hook,
                             GlobalHotkey& hotkey,
                             const llm::Pricing& pricing,
                             const std::filesystem::path& dataDir,
                             QObject* parent)
    : QObject(parent),
      storage_(store),
      cost_(storage_.statsStore(), llm, pricing, [] { return QDate::currentDate(); }, this), capture_(storage_, hook), catalog_(storage_, llm, this), explanation_(storage_, llm, cost_, this), updateClient_(update::Settings::load(dataDir / "update.json"), nullptr, this), updateDownloader_(update::Settings::load(dataDir / "update.json"), nullptr, this), updateDuty_(storage_, updateClient_, updateDownloader_, QString::fromUtf8(LENS_VERSION), Launcher{}, QString{}, [] { return QDate::currentDate(); }, this), llm_(llm), hotkey_(hotkey)
{
    budgetPaused_ = cost_.budgetStatus().exhausted;
    connect(&capture_, &CaptureDuty::selectionBarRequested, this, &AppController::selectionBarRequested);
    connect(&capture_, &CaptureDuty::selectionBarRequested, this, [this] { emit petEvent(core::pet::PetEvent::SelectionShown); });
    connect(&capture_, &CaptureDuty::selectionActionRequested, this, [this](QString action, QString text) {
        explanation_.runSelectionAction(std::move(action), std::move(text));
    });
    connect(&capture_, &CaptureDuty::pointerPressed, this, &AppController::pointerPressed);
    connect(&capture_, &CaptureDuty::selectionReady, this, [this](PendingSelection selection) {
        explanation_.setSelection(std::move(selection));
    });
    connect(&capture_, &CaptureDuty::scanCandidatesReady, this, [this](QVariantList candidates, QPoint anchor) {
        explanation_.enqueueScanCandidates(std::move(candidates), anchor);
    });
    connect(&capture_, &CaptureDuty::screenshotFailed, this, &AppController::reportCaptureRefusal);
    connect(&capture_, &CaptureDuty::settingsChanged, this, &AppController::settingsChanged);
    connect(&explanation_, &ExplanationDuty::bubbleChanged, this, &AppController::bubbleChanged);
    connect(&explanation_, &ExplanationDuty::noticeChanged, this, &AppController::noticeChanged);
    connect(&catalog_, &ModelCatalogDuty::settingsChanged, this, &AppController::settingsChanged);
    connect(&catalog_, &ModelCatalogDuty::modelsChanged, this, &AppController::modelsChanged);
    connect(&explanation_, &ExplanationDuty::busyChanged, this, [this] {
        refreshCaptureGates();
        emit busyChanged();
    });
    connect(&explanation_, &ExplanationDuty::statsChanged, this, [this] {
        emit statsChanged();
        emit yearDaysChanged();
    });
    connect(&explanation_, &ExplanationDuty::petEvent, this, &AppController::petEvent);
    connect(&cost_, &CostDuty::stateChanged, this, [this] {
        refreshCaptureGates();
        explanation_.resumeQueued();
        emit statsChanged();
        emit yearDaysChanged();
        emit settingsChanged();
        emit dailyBudgetChanged();
        // The pet sleeps while the daily budget is spent and wakes once it is not, one event per change.
        const bool paused = cost_.budgetStatus().exhausted;
        if (paused != budgetPaused_) {
            budgetPaused_ = paused;
            emit petEvent(paused ? core::pet::PetEvent::BudgetPaused : core::pet::PetEvent::BudgetResumed);
        }
    });

    connect(&updateDuty_, &UpdateDuty::newVersionOffered, this, [this] { emit petEvent(core::pet::PetEvent::UpdateAvailable); });
    connect(&hotkey_, &GlobalHotkey::pressed, this, &AppController::onTriggerHotkey);
    hotkey_.setKeys(storedHotkey(storage_));
    llm.setExplanationLang(QString::fromStdString(store.explanationLang()));
    restoreModelService();
    cost_.setLegacyModel(llm_.model());
    gateTimer_.setInterval(1000);
    connect(&gateTimer_, &QTimer::timeout, this, [this] {
        refreshCaptureGates();
        explanation_.resumeQueued();
    });
    gateTimer_.start();
    refreshCaptureGates();
    LENS_INFO("AppController ready: level={} explanation language '{}'", store.level(), store.explanationLang());
}

AppController::~AppController() = default;

void AppController::restoreModelService()
{
    auto const keys        = storage_.section();
    const QString provider = catalog_.currentProvider();
    if (!llm_.setProvider(provider))
        llm_.setProvider(QStringLiteral("custom"));
    const QString url = QString::fromStdString(keys.value<std::string>("URL", {})).trimmed();
    if (!url.isEmpty()) {
        const QUrl savedUrl(url);
        if (savedUrl.isValid() && savedUrl.scheme() == QLatin1String("https") && !savedUrl.host().isEmpty())
            llm_.setBaseUrl(savedUrl);
        else
            LENS_WARN("ignored an invalid persisted model service URL");
    }
    const QString model = QString::fromStdString(keys.value<std::string>("MODEL", {})).trimmed();
    if (!model.isEmpty())
        llm_.setModel(model);
    // The shared model source, asked once per run: one unauthenticated GET that costs nothing, and
    // the slices it answers with are what the model field then offers (docs/adr/0020).
    catalog_.refreshModels();
}

AppController* AppController::instance_ = nullptr;

void AppController::provide(AppController* instance)
{
    instance_ = instance;
}

AppController* AppController::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    Q_ASSERT(instance_ != nullptr);
    QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
    return instance_;
}

void AppController::onSelectionReleased(QPoint anchor)
{
    capture_.onSelectionReleased(anchor);
}

QPoint AppController::cursorPos() const
{
    return QCursor::pos();
}

void AppController::runSelectionAction(QString action, QString text)
{
    explanation_.runSelectionAction(std::move(action), std::move(text));
}

void AppController::mark(QString lemma, bool learned)
{
    explanation_.mark(std::move(lemma), learned);
}

void AppController::setAutoScan(bool on)
{
    capture_.setAutoScan(on);
}

void AppController::setOcrCapture(bool on)
{
    capture_.setOcrCapture(on);
}

void AppController::setMinimumWordLength(int length)
{
    capture_.setMinimumWordLength(length);
}

bool AppController::setScanWhitelist(QString processes)
{
    return capture_.setScanWhitelist(std::move(processes));
}

void AppController::setTesseractExecutable(QUrl executable)
{
    capture_.setTesseractExecutable(executable.toLocalFile());
}

void AppController::setTesseractDataDirectory(QUrl directory)
{
    capture_.setTesseractDataDirectory(directory.toLocalFile());
}

void AppController::restoreCaptureDefaults()
{
    capture_.restoreDefaults();
    // The trigger key is one of the capture settings, so the way back from every one of them is
    // the way back from this one too.
    const QKeyCombination fallback = GlobalHotkey::defaultKeys()[0];
    setOcrHotkey(fallback.key(), fallback.keyboardModifiers());
}

QString AppController::setOcrHotkey(int key, int modifiers)
{
    const QKeySequence sequence{QKeyCombination{Qt::KeyboardModifiers(modifiers), Qt::Key(key)}};
    if (not GlobalHotkey::isUsable(sequence)) return QStringLiteral("unusable");
    if (not hotkey_.setKeys(sequence)) return QStringLiteral("taken");
    const QString text = sequence.toString(QKeySequence::PortableText);
    storage_.section().set<std::string>("ocrHotkey", text.toStdString());
    LENS_INFO("OCR trigger key set to '{}'", text.toStdString());
    emit settingsChanged();
    return {};
}

QString AppController::captureRegion(QRect region)
{
    const auto refusal = capture_.captureRegion(region);
    if (not refusal) return {};
    reportCaptureRefusal(*refusal);
    // The token, not the sentence: what crosses to QML is the same byte string it crossed as
    // before, and the notice the reader actually reads is the one raised just above.
    return QString::fromLatin1(refusalKey(*refusal));
}

bool AppController::installHotkey()
{
    const bool registered = hotkey_.install();
    if (not registered)
        LENS_WARN("the OCR trigger key is not in force; the capture mask cannot be raised from the keyboard");
    emit settingsChanged();
    return registered;
}

void AppController::onTriggerHotkey()
{
    const auto refusal = capture_.captureRefusalReason();
    // The one line that says the key arrived. Without it a hotkey that never reached the process
    // and a mask that never came up read the same in the log: nothing at all.
    LENS_INFO("OCR trigger key pressed; capture can {}start", refusal ? "not " : "");
    if (refusal) {
        reportCaptureRefusal(*refusal);
        return;
    }
    // Pressing the key again while the mask is up is not a second capture: the mask is already
    // taking the pointer, and raising it again changes nothing.
    emit captureMaskRequested();
}

void AppController::reportCaptureRefusal(CaptureRefusal const refusal)
{
    // The runtime's own states are worded where the settings page words them, so a reader who is
    // told why from here reads the same sentence they read beside the switch. Every context is
    // written out at its call: lupdate reads literal arguments only, and a context in a variable
    // extracts nothing.
    //
    // A switch with no default arm, so a reason added to CaptureRefusal has to be given a sentence
    // here instead of falling into the last one. Four of them share a sentence today, and that is
    // written out case by case rather than left to whatever the chain ended with.
    QString body;
    switch (refusal) {
        case CaptureRefusal::ScreenshotOff: body = tr("Screenshot capture is off."); break;
        case CaptureRefusal::BudgetPaused: body = tr("Today's budget is used up."); break;
        case CaptureRefusal::Busy: body = tr("A recognition is already running."); break;
        case CaptureRefusal::OutsideScreen:
        case CaptureRefusal::RegionTooLarge: body = tr("That region cannot be captured."); break;
        case CaptureRefusal::EmptyText: body = tr("No text was found in that region."); break;
        case CaptureRefusal::InvalidImage: body = tr("That region could not be read."); break;
        case CaptureRefusal::Checking: body = QCoreApplication::translate("SettingsPopup", "Checking OCR"); break;
        case CaptureRefusal::LanguageMissing: body = QCoreApplication::translate("SettingsPopup", "English OCR data is missing"); break;
        case CaptureRefusal::RuntimeMissing:
        case CaptureRefusal::StartFailed:
        case CaptureRefusal::TimedOut:
        case CaptureRefusal::Failed: body = QCoreApplication::translate("SettingsPopup", "OCR runtime is unavailable"); break;
    }
    LENS_INFO("OCR capture was not started ({})", refusalKey(refusal));
    explanation_.showNotice(QCoreApplication::translate("SettingsPopup", "OCR"), body, QString::fromLatin1(kNoticeError));
}

void AppController::setLevel(int level)
{
    storage_.knownStore().setLevel(level);
    storage_.save();
    LENS_INFO("level set to {}", level);
    emit settingsChanged();
}

void AppController::setExplanationLang(QString lang)
{
    storage_.knownStore().setExplanationLang(lang.toStdString());
    llm_.setExplanationLang(lang);
    storage_.save();
    LENS_INFO("explanation language set to '{}'", lang.toStdString());
    emit settingsChanged();
    emit statsChanged();
}

void AppController::setMultiSense(bool on)
{
    storage_.section().set<bool>("multiSense", on);
    emit settingsChanged();
}

void AppController::setEtymology(bool on)
{
    storage_.section().set<bool>("etymology", on);
    emit settingsChanged();
}

void AppController::setTheme(QString theme)
{
    storage_.section().set<std::string>("theme", theme.toStdString());
    emit settingsChanged();
}

void AppController::setAnimationsEnabled(bool on)
{
    storage_.section().set<bool>("animationsEnabled", on);
    emit settingsChanged();
}

void AppController::setUiLanguage(QString lang)
{
    storage_.section().set<std::string>("uiLanguage", lang.toStdString());
    emit uiLanguageChanged(lang);
    emit settingsChanged();
    emit statsChanged();
}

void AppController::setSelectionCapture(bool on)
{
    storage_.section().set<bool>("selectionCapture", on);
    LENS_INFO("selection capture {}", on ? "on" : "off");
    emit settingsChanged();
}

void AppController::setApiKey(QString key)
{
    llm_.setApiKey(key);
    storage_.section().set<std::string>("API-KEY", key.toStdString());
    LENS_INFO("API key {}", key.isEmpty() ? "cleared" : "updated");
    // The source takes no key, so the field already has a list and a key change does not unlock one
    // (docs/adr/0020). The trigger stays because the request is free and this is the moment the
    // reader is most likely looking at the field.
    catalog_.refreshModels();
    emit settingsChanged();
}

void AppController::setClipboardPolicy(QString policy)
{
    if (policy != QLatin1String("topmost") && policy != QLatin1String("silent")) {
        LENS_WARN("unknown clipboard policy '{}'; leaving the setting alone", policy.toStdString());
        return;
    }
    storage_.section().set<std::string>("clipboardPolicy", policy.toStdString());
    LENS_INFO("clipboard policy set to '{}'", policy.toStdString());
    emit settingsChanged();
}

void AppController::setProvider(QString provider)
{
    // No emit here: the duty announces the change itself, and it is the one that also hears the
    // model list arriving later.
    catalog_.setProvider(std::move(provider));
}

void AppController::refreshModels()
{
    catalog_.refreshModels();
}

void AppController::setModel(QString model)
{
    if (catalog_.setModel(model))
        emit settingsChanged();
}

void AppController::setApiUrl(QString url)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https")) {
        LENS_WARN("refused a non-HTTPS model service URL");
        return;
    }
    llm_.setBaseUrl(parsed);
    storage_.section().set<std::string>("URL", parsed.toString().toStdString());
    // The source is one fixed endpoint, so a new address does not move where the list comes from
    // and the models in hand are not the old service's. The refresh is kept only because the
    // triggers were deliberately left alone (docs/adr/0020); the request is free.
    catalog_.refreshModels();
    emit settingsChanged();
}

void AppController::setAutostart(bool on)
{
    writeAutostart(on);
    emit settingsChanged();
}

void AppController::bubbleHoverChanged(bool hovering)
{
    LENS_TRACE("bubble hover {}", hovering);
}

void AppController::dismissBubble()
{
    explanation_.dismissBubble();
}

void AppController::dismissNotice()
{
    explanation_.dismissNotice();
}

void AppController::reviewWord(QString lemma, int x, int y)
{
    explanation_.reviewWord(std::move(lemma), QPoint(x, y));
}

void AppController::dismissReview()
{
    explanation_.dismissReview();
}

void AppController::explainLemma(QString lemma)
{
    // The one path a word list can spend money through, and it is a button the reader pressed
    // rather than the hover that put the button there.
    if (not cost_.canRequest()) {
        explanation_.showNotice(lemma, tr("Today's budget is used up."), QString::fromLatin1(kNoticeError));
        return;
    }
    explanation_.explainLemma(std::move(lemma));
}

QVariantMap AppController::bubble() const
{
    return explanation_.bubble();
}

QVariantMap AppController::notice() const
{
    return explanation_.notice();
}

QVariantMap AppController::settings()
{
    auto const keys        = storage_.section();
    const QString provider = catalog_.currentProvider();
    QVariantMap result{{"level", storage_.knownStore().level()},
                       {"levels", StorageDuty::levelOptions()},
                       {"explanationLang", QString::fromStdString(storage_.knownStore().explanationLang())},
                       {"multiSense", keys.value<bool>("multiSense", false)},
                       {"etymology", keys.value<bool>("etymology", false)},
                       {"theme", QString::fromStdString(keys.value<std::string>("theme", "light"))},
                       {"animationsEnabled", keys.value<bool>("animationsEnabled", true)},
                       {"uiLanguage", QString::fromStdString(keys.value<std::string>("uiLanguage", "zh"))},
                       {"hasApiKey", !keys.value<std::string>("API-KEY", {}).empty()},
                       {"selectionCapture", keys.value<bool>("selectionCapture", true)},
                       {"clipboardPolicy", QString::fromStdString(keys.value<std::string>("clipboardPolicy", "topmost"))},
                       {"ocrHotkey", hotkey_.keys().toString(QKeySequence::PortableText)},
                       {"ocrHotkeyConflicted", hotkey_.conflicted()},
                       {"provider", provider},
                       {"model", llm_.model()},
                       {"url", llm_.baseUrl().toString()},
                       {"dailyBudget", dailyBudget()},
                       {"autostart", autostartEnabled()}};
    const auto captureSettings = capture_.settings();
    for (auto it = captureSettings.cbegin(); it != captureSettings.cend(); ++it)
        result.insert(it.key(), it.value());
    result.insert(QStringLiteral("ocrStatus"), capture_.ocrStatus());
    const auto explanationSettings = catalog_.settings();
    for (auto it = explanationSettings.cbegin(); it != explanationSettings.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QVariantList AppController::models() const
{
    return catalog_.models();
}

QVariantMap AppController::stats() const
{
    return cost_.stats();
}

QVariantList AppController::words() const
{
    const QDate today = QDate::currentDate();
    QSet<QString> seen;
    QHash<QString, int> pops;
    const auto& history = storage_.statsStore().history();
    for (const core::HistoryEntry& entry : history)
        ++pops[QString::fromStdString(entry.lemma)];

    QVariantList out;
    out.reserve(static_cast<qsizetype>(history.size()));
    for (const core::HistoryEntry& entry : history) {
        const QString shown = QString::fromStdString(entry.lemma);
        if (seen.contains(shown))
            continue;
        seen.insert(shown);

        const QDateTime moment = QDateTime::fromString(QString::fromStdString(entry.minute), QStringLiteral("yyyy-MM-dd HH:mm"));
        QString when;
        if (moment.isValid()) {
            const qint64 daysAgo = moment.date().daysTo(today);
            if (daysAgo == 0)
                when = tr("Today %1").arg(moment.toString(QStringLiteral("HH:mm")));
            else if (daysAgo == 1)
                when = tr("Yesterday %1").arg(moment.toString(QStringLiteral("HH:mm")));
            else
                when = moment.toString(QStringLiteral("MM-dd HH:mm"));
        }

        QString status;
        if (entry.verdict == "known")
            status = tr("Known");
        else if (entry.verdict == "new")
            status = tr("New");

        out.append(QVariantMap{{"word", shown},
                               {"when", when},
                               {"verdict", QString::fromStdString(entry.verdict)},
                               {"status", status},
                               {"pops", pops.value(shown)}});
    }
    return out;
}

QVariantList AppController::yearDays() const
{
    return yearGrid(QDate::currentDate(), storage_.statsStore().daily());
}

QString AppController::exportWords(QString scope)
{
    core::ExportScope which;
    if (scope == QLatin1String("all"))
        which = core::ExportScope::All;
    else if (scope == QLatin1String("known"))
        which = core::ExportScope::Known;
    else if (scope == QLatin1String("new"))
        which = core::ExportScope::New;
    else {
        LENS_WARN("export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return {};
    }
    const QString text = QString::fromStdString(core::exportLemmas(storage_.statsStore().history(), which));
    LENS_INFO("words export for '{}': {} character(s) ready", scope.toStdString(), text.size());
    return text;
}

bool AppController::saveWords(QUrl path, QString scope)
{
    if (scope != QLatin1String("all") && scope != QLatin1String("known") && scope != QLatin1String("new")) {
        LENS_WARN("words export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return false;
    }
    const QString local = path.toLocalFile();
    if (local.isEmpty()) {
        LENS_WARN("words export was given '{}', which names no local file", path.toString().toStdString());
        return false;
    }
    const QString text = exportWords(scope);
    QSaveFile file(local);
    if (!file.open(QIODevice::WriteOnly)) {
        LENS_WARN("words export could not open '{}' for writing", local.toStdString());
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        LENS_WARN("words export failed to write '{}'", local.toStdString());
        return false;
    }
    LENS_INFO("words export for '{}': {} character(s) written to '{}'", scope.toStdString(), text.size(), local.toStdString());
    return true;
}

bool AppController::setDailyBudget(double amount)
{
    if (!cost_.setDailyBudget(amount))
        return false;
    storage_.save();
    return true;
}

bool AppController::removeWord(QString lemma)
{
    const auto value = lemma.trimmed();
    if (value.isEmpty())
        return false;
    explanation_.invalidateWord(value);
    const bool removed = storage_.removeWord(value);
    if (removed) {
        emit statsChanged();
        emit yearDaysChanged();
        emit bubbleChanged();
    }
    return removed;
}

QVariantMap AppController::cost() const
{
    return cost_.cost();
}

void AppController::checkForUpdatesAtStartup()
{
    updateDuty_.checkAtStartup();
}

UpdateDuty* AppController::updateDuty()
{
    return &updateDuty_;
}

QString AppController::modeLabel() const
{
    return capture_.settings().value("autoScan").toBool() ? tr("Auto") : tr("Manual");
}

QString AppController::busyLabel() const
{
    return explanation_.busyLabel();
}

double AppController::dailyBudget() const
{
    return storage_.statsStore().dailyBudget();
}

void AppController::refreshCaptureGates()
{
    const bool budgetPaused = !cost_.canScan();
    capture_.setBudgetPaused(budgetPaused);
    capture_.setScanPaused(budgetPaused || !explanation_.busyLabel().isEmpty());
}

}
