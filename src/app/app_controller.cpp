/**
 * @file app_controller.cpp
 * @brief QML facade that wires the application duties to the surfaces.
 */

#include "app_controller.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QQmlEngine>
#include <QJSEngine>

#include <algorithm>
#include <utility>

#include "autostart.h"
#include "bubble_settings.h"
#include "glass_settings.h"
#include "notice.h"
#include "paths.h"
#include "pet/pet_controller.h"
#include "reveal_settings.h"
#include "settings_defaults.h"
#include "util/text.h"
#include "update/update_client.h"
#include "util/log.h"
#include "words_view.h"
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

/// @return `data/preview.json` as a map, empty when it is missing or unreadable.
///
/// The settings preview's demo card is a nicety, and one it cannot read is not worth a refused
/// start: an empty map makes PreviewSample.payload() answer null, and the preview then does not
/// come up at all. Only the path is logged -- the reading that failed is the file's, not a field's.
QVariantMap loadPreviewSample(const std::filesystem::path& file)
{
    QFile document(widen(file));
    if (not document.open(QIODevice::ReadOnly)) {
        LENS_WARN("no settings preview sample at '{}'; the preview stays down", narrow(file));
        return {};
    }
    const auto parsed = QJsonDocument::fromJson(document.readAll());
    if (not parsed.isObject()) {
        LENS_WARN("'{}' is not a JSON object; the preview stays down", narrow(file));
        return {};
    }
    return parsed.object().toVariantMap();
}

/// @return The face a reader's font setting draws with: the stored family when this machine has it, else the face the
///         application font draws Latin with, which is the first family of the list main() installed. The default is that
///         face rather than an empty family, which QML would leave unresolved. Without a GUI application there is no font
///         database and no application font, so the answer is the empty family; only the unit tests run that way.
QString fontFamilyFor(QString const& stored)
{
    if (not qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return {};
    if (QFontDatabase::hasFamily(stored)) return stored;
    const QStringList families = QGuiApplication::font().families();
    return families.isEmpty() ? QString{} : families.first();
}

}

AppController::AppController(core::KnownStore&            store,
                             llm::LlmClient&              llm,
                             MouseSelectionHook&          hook,
                             GlobalHotkey&                hotkey,
                             const llm::Pricing&          pricing,
                             const std::filesystem::path& dataDir,
                             QObject*                     parent)
    : QObject(parent),
      storage_(store),
      cost_(storage_.statsStore(), llm, pricing, [] { return QDate::currentDate(); }, this), capture_(storage_, hook), catalog_(storage_, llm, this), explanation_(storage_, llm, cost_, this), updateClient_(update::Settings::load(dataDir / "update.json"), nullptr, this), updateDownloader_(update::Settings::load(dataDir / "update.json"), nullptr, this), updateDuty_(storage_, updateClient_, updateDownloader_, QString::fromUtf8(LENS_VERSION), Launcher{}, QString{}, [] { return QDate::currentDate(); }, this), sync_(storage_, dataDir, this), reviewDuty_(storage_, [this] { return pet_ != nullptr and pet_->enabled(); }, [this] { return pet_ != nullptr and pet_->availableForReview(); }, this), llm_(llm), hotkey_(hotkey), previewSample_(loadPreviewSample(dataDir / "preview.json"))
{
    budgetPaused_ = cost_.budgetStatus().exhausted;
    glass_.setQuality(glassQualityFrom(storage_.section().value<int>("glassQuality", defaults::kGlassQuality)));
    // A change to the statistics or the settings is what a sync carries, so either one schedules
    // a sync. Marks reach here as statsChanged too, since ExplanationDuty forwards it.
    connect(this, &AppController::statsChanged, &sync_, &SyncDuty::scheduleSync);
    connect(this, &AppController::settingsChanged, &sync_, &SyncDuty::scheduleSync);
    connect(&sync_, &SyncDuty::remoteApplied, this, [this] {
        emit settingsChanged();
        emit statsChanged();
    });
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
    connect(&catalog_, &ModelCatalogDuty::settingsChanged, this, &AppController::settingsChanged);
    connect(&explanation_, &ExplanationDuty::busyChanged, this, [this] {
        refreshCaptureGates();
        emit busyChanged();
    });
    connect(&explanation_, &ExplanationDuty::statsChanged, this, [this] {
        emit statsChanged();
        emit yearDaysChanged();
    });
    // A pop is recorded by ExplanationDuty straight into the store, so the duty that projects
    // today's figures has to be told as well; QML now reads them from Controller.costDuty.
    connect(&explanation_, &ExplanationDuty::statsChanged, &cost_, &CostDuty::statsChanged);
    connect(&explanation_, &ExplanationDuty::petEvent, this, &AppController::petEvent);
    connect(&reviewDuty_, &ReviewDuty::wordChanged, this, &AppController::reviewQuestionChanged);
    connect(&reviewDuty_, &ReviewDuty::petEvent, this, &AppController::petEvent);
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
    auto const    keys     = storage_.section();
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
    Q_UNUSED(scriptEngine)
    Q_ASSERT(instance_ != nullptr);
    // The glass picture is served as an image, so the engine that loads the surfaces is where it is registered.
    if (engine != nullptr and engine->imageProvider(QStringLiteral("glass")) == nullptr)
        engine->addImageProvider(QStringLiteral("glass"), new GlassImageProvider(instance_->glass_));
    QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
    return instance_;
}

QPoint AppController::cursorPos() const
{
    return QCursor::pos();
}

QRect AppController::screenWorkArea(QString deviceName) const
{
    // An empty name names no screen. Without this guard the loop would match the first unnamed screen,
    // the offscreen platform's, and hand its area to a caller that had no name.
    if (deviceName.isEmpty()) return {};
    for (auto const* screen : QGuiApplication::screens())
        if (screen->name() == deviceName)
            return screen->availableGeometry();
    return {};
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

void AppController::restoreDefaults()
{
    // Written through the ordinary setters so the clamps and the validation stay the only way a
    // value reaches the document; what is centralized here is the value, not the write.
    setLevel(defaults::kLevel);
    setExplanationLang(QString::fromStdString(std::string{defaults::kExplanationLang}));
    setMultiSense(defaults::kMultiSense);
    setEtymology(defaults::kEtymology);
    setExample(defaults::kExample);
    setTheme(QString::fromStdString(std::string{defaults::kTheme}));
    setFont(QString{});
    setBubbleFontSize(defaults::kBubbleFontSize);
    setAnimationsEnabled(defaults::kAnimationsEnabled);
    setRevealSpeed(defaults::kRevealSpeed);
    setSelectionCapture(defaults::kSelectionCapture);
    setClipboardPolicy(defaults::kClipboardSilent);
    setBubbleDismissSeconds(defaults::kBubbleDismissSeconds);
    setBubblePlacement(QString::fromStdString(std::string{defaults::kBubblePlacement}));
    setSelectionBarStyle(QString::fromStdString(std::string{defaults::kSelectionBarStyle}));
    setFrostedBlur(defaults::kFrostedBlurDefault);
    setFrostedSaturation(defaults::kFrostedSaturationDefault);
    setFrostedRim(defaults::kFrostedRimDefault);
    setLiquidRefraction(defaults::kLiquidRefractionDefault);
    setLiquidBlur(defaults::kLiquidBlurDefault);
    setLiquidRim(defaults::kLiquidRimDefault);
    setLiquidTextColor(QString::fromStdString(std::string{defaults::kLiquidTextColor}));
    setGlassQuality(defaults::kGlassQuality);
    setReviewFrequency(static_cast<int>(defaults::kReviewFrequency));
    if (not catalog_.setProvider(defaults::defaultProviderName()))
        LENS_WARN("the catalog has no usable default provider; the model service was left alone");
    setModel(defaults::defaultModelName());
    restoreCaptureDefaults();
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

QString AppController::reviewQuestion() const
{
    return reviewDuty_.word();
}

void AppController::bindPet(pet::PetController* pet)
{
    pet_ = pet;
}

void AppController::answerReview(bool const remembered, int const x, int const y)
{
    auto const lemma = reviewDuty_.answer(remembered, QDateTime::currentMSecsSinceEpoch());
    if (lemma.isEmpty())
        return;
    explanation_.showReviewAnswer(lemma, QPoint{x, y});
}

void AppController::setReviewFrequency(int const value)
{
    auto const clamped = std::clamp(value, static_cast<int>(core::ReviewFrequency::Off), static_cast<int>(core::kLastReviewFrequency));
    reviewDuty_.setFrequency(static_cast<core::ReviewFrequency>(clamped), QDateTime::currentMSecsSinceEpoch());
    emit settingsChanged();
}

void AppController::setExplanationLang(QString lang)
{
    storage_.knownStore().setExplanationLang(lang.toStdString());
    storage_.save();
    LENS_INFO("explanation language set to '{}'", lang.toStdString());
    emit settingsChanged();
    emit statsChanged();
}

void AppController::reexplainIn(QString language)
{
    if (language.isEmpty() or language == QString::fromStdString(storage_.knownStore().explanationLang()))
        return;
    setExplanationLang(std::move(language));
    explanation_.reexplain();
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

void AppController::setExample(bool on)
{
    storage_.section().set<bool>("example", on);
    emit settingsChanged();
}

void AppController::setTheme(QString theme)
{
    storage_.section().set<std::string>("theme", theme.toStdString());
    emit settingsChanged();
}

void AppController::setFont(QString family)
{
    const QString chosen = family.trimmed();
    if (not chosen.isEmpty() and not QFontDatabase::hasFamily(chosen)) {
        LENS_WARN("unknown font family '{}'; leaving the setting alone", chosen.toStdString());
        return;
    }
    storage_.section().set<std::string>("font", chosen.toStdString());
    emit settingsChanged();
}

QVariantList AppController::latinFontFamilies() const
{
    // Latin only: this is the face English is read in, and a family with no Latin cannot even
    // write its own name in the field. The "@" entries are the vertical-writing duplicates of the
    // families beside them. The database hands the list over sorted and without repeats.
    QVariantList options;
    for (const QString& family : QFontDatabase::families(QFontDatabase::Latin)) {
        if (family.startsWith(u'@'))
            continue;
        options.append(QVariantMap{{"value", family}, {"label", family}, {"group", QString{}}, {"note", QString{}}});
    }
    return options;
}

void AppController::setBubbleFontSize(int pixels)
{
    storage_.section().set<int>("bubbleFontSize", clampBubbleFontSize(pixels));
    emit settingsChanged();
}

QVariantList AppController::bubbleFontSizeOptions() const
{
    // Every whole pixel of the range: seven rows, which is a list the reader reads down rather
    // than scrolls, and the preview beside it is what says which one is right.
    QVariantList options;
    for (int pixels = kBubbleFontSizeMin; pixels <= kBubbleFontSizeMax; ++pixels) {
        options.append(QVariantMap{{"value", pixels},
                                   {"label", QCoreApplication::translate("lens::app::AppController", "%1 px").arg(pixels)},
                                   {"group", QString{}},
                                   {"note", pixels == kBubbleFontSizeDefault ? QCoreApplication::translate("lens::app::AppController", "Default") : QString{}}});
    }
    return options;
}

void AppController::setFrostedBlur(int percent)
{
    storage_.section().set<int>("frostedBlur", clampGlass(kFrostedBlur, percent));
    emit settingsChanged();
}

void AppController::setFrostedSaturation(int percent)
{
    storage_.section().set<int>("frostedSaturation", clampGlass(kFrostedSaturation, percent));
    emit settingsChanged();
}

void AppController::setFrostedRim(int percent)
{
    storage_.section().set<int>("frostedRim", clampGlass(kFrostedRim, percent));
    emit settingsChanged();
}

void AppController::setLiquidRefraction(int pixels)
{
    storage_.section().set<int>("liquidRefraction", clampGlass(kLiquidRefraction, pixels));
    emit settingsChanged();
}

void AppController::setLiquidBlur(int percent)
{
    storage_.section().set<int>("liquidBlur", clampGlass(kLiquidBlur, percent));
    emit settingsChanged();
}

void AppController::setLiquidRim(int percent)
{
    storage_.section().set<int>("liquidRim", clampGlass(kLiquidRim, percent));
    emit settingsChanged();
}

void AppController::setLiquidTextColor(QString color)
{
    static const QRegularExpression hexColor{QStringLiteral("^#[0-9a-fA-F]{6}$")};
    if (not hexColor.match(color).hasMatch()) {
        LENS_WARN("unknown liquid text colour '{}'; leaving the setting alone", color.toStdString());
        return;
    }
    storage_.section().set<std::string>("liquidTextColor", color.toLower().toStdString());
    emit settingsChanged();
}

void AppController::setGlassQuality(int quality)
{
    auto const chosen = glassQualityFrom(quality);
    storage_.section().set<int>("glassQuality", static_cast<int>(chosen));
    glass_.setQuality(chosen);
    emit settingsChanged();
}

void AppController::setAnimationsEnabled(bool on)
{
    storage_.section().set<bool>("animationsEnabled", on);
    emit settingsChanged();
}

void AppController::setRevealSpeed(int speed)
{
    storage_.section().set<int>("revealSpeed", clampRevealSpeed(speed));
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

void AppController::setClipboardPolicy(bool const silent)
{
    const ClipboardPolicy policy = silent ? ClipboardPolicy::Silent : ClipboardPolicy::Topmost;
    storage_.section().set<std::string>("clipboardPolicy", std::string{clipboardPolicyKey(policy)});
    LENS_INFO("clipboard policy set to '{}'", clipboardPolicyKey(policy));
    emit settingsChanged();
}

void AppController::setBubbleDismissSeconds(int seconds)
{
    storage_.section().set<int>("bubbleDismissSeconds", clampBubbleDismissSeconds(seconds));
    emit settingsChanged();
}

void AppController::setBubblePlacement(QString placement)
{
    if (placement != QLatin1String("above") && placement != QLatin1String("below")) {
        LENS_WARN("unknown bubble placement '{}'; leaving the setting alone", placement.toStdString());
        return;
    }
    storage_.section().set<std::string>("bubblePlacement", placement.toStdString());
    emit settingsChanged();
}

void AppController::setSelectionBarStyle(QString style)
{
    if (style != QLatin1String("bar") && style != QLatin1String("tab") && style != QLatin1String("hand") && style != QLatin1String("tag") && style != QLatin1String("fan")) {
        LENS_WARN("unknown selection bar style '{}'; leaving the setting alone", style.toStdString());
        return;
    }
    storage_.section().set<std::string>("selectionBarStyle", style.toStdString());
    emit settingsChanged();
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

void AppController::reviewWord(QString lemma, int x, int y)
{
    explanation_.reviewWord(std::move(lemma), QPoint(x, y));
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

QVariantMap AppController::settings()
{
    auto const    keys     = storage_.section();
    const QString provider = catalog_.currentProvider();
    // The slider shows the level; the pace the reveal takes is that level through the mapping in
    // reveal_settings.h. Read once so the two the map hands out cannot drift apart.
    const int revealSpeed = clampRevealSpeed(keys.value<int>("revealSpeed", defaults::kRevealSpeed));
    // The face the English runs are drawn in: the reader's pick, or the application's own when
    // nothing is stored -- and also when what is stored names a family this machine no longer has,
    // which a settings document copied from another machine can. Handed out as a family rather
    // than as the empty string an unset setting holds, because QML binds it straight onto a Text
    // and an empty family there would put the run back to a font nobody chose.
    const QString storedFont = QString::fromStdString(keys.value<std::string>("font", {}));
    QVariantMap   result{{"level", storage_.knownStore().level()},
                         {"levels", StorageDuty::levelOptions()},
                         {"reviewFrequency", static_cast<int>(reviewDuty_.frequency())},
                         {"reviewFrequencyOptions", StorageDuty::reviewFrequencyOptions()},
                         {"explanationLang", QString::fromStdString(storage_.knownStore().explanationLang())},
                         {"multiSense", keys.value<bool>("multiSense", defaults::kMultiSense)},
                         {"etymology", keys.value<bool>("etymology", defaults::kEtymology)},
                         {"example", keys.value<bool>("example", defaults::kExample)},
                         {"theme", QString::fromStdString(keys.value<std::string>("theme", std::string{defaults::kTheme}))},
                         {"font", fontFamilyFor(storedFont)},
                         {"bubbleFontSize", clampBubbleFontSize(keys.value<int>("bubbleFontSize", defaults::kBubbleFontSize))},
                         {"frostedBlur", clampGlass(kFrostedBlur, keys.value<int>("frostedBlur", kFrostedBlur.fallback))},
                         {"frostedSaturation", clampGlass(kFrostedSaturation, keys.value<int>("frostedSaturation", kFrostedSaturation.fallback))},
                         {"frostedRim", clampGlass(kFrostedRim, keys.value<int>("frostedRim", kFrostedRim.fallback))},
                         {"liquidRefraction", clampGlass(kLiquidRefraction, keys.value<int>("liquidRefraction", kLiquidRefraction.fallback))},
                         {"liquidBlur", clampGlass(kLiquidBlur, keys.value<int>("liquidBlur", kLiquidBlur.fallback))},
                         {"liquidRim", clampGlass(kLiquidRim, keys.value<int>("liquidRim", kLiquidRim.fallback))},
                         {"liquidTextColor", QString::fromStdString(keys.value<std::string>("liquidTextColor", std::string{kLiquidTextColorFallback}))},
                         {"glassQuality", static_cast<int>(glassQualityFrom(keys.value<int>("glassQuality", defaults::kGlassQuality)))},
                         {"animationsEnabled", keys.value<bool>("animationsEnabled", defaults::kAnimationsEnabled)},
                         {"revealSpeed", revealSpeed},
                         {"revealCharacterMs", revealMillisPerCharacter(revealSpeed)},
                         {"uiLanguage", QString::fromStdString(keys.value<std::string>("uiLanguage", std::string{defaults::kUiLanguage}))},
                         {"hasApiKey", !keys.value<std::string>("API-KEY", {}).empty()},
                         {"selectionCapture", keys.value<bool>("selectionCapture", defaults::kSelectionCapture)},
                         {"clipboardSilent", clipboardPolicyFromStored(keys.value<std::string>("clipboardPolicy", {})) == ClipboardPolicy::Silent},
                         {"bubbleDismissSeconds", clampBubbleDismissSeconds(keys.value<int>("bubbleDismissSeconds", defaults::kBubbleDismissSeconds))},
                         {"bubblePlacement", QString::fromStdString(keys.value<std::string>("bubblePlacement", std::string{defaults::kBubblePlacement}))},
                         {"selectionBarStyle", QString::fromStdString(keys.value<std::string>("selectionBarStyle", std::string{defaults::kSelectionBarStyle}))},
                         {"ocrHotkey", hotkey_.keys().toString(QKeySequence::PortableText)},
                         {"ocrHotkeyConflicted", hotkey_.conflicted()},
                         {"provider", provider},
                         {"model", llm_.model()},
                         {"url", llm_.baseUrl().toString()},
                         {"dailyBudget", dailyBudget()},
                         {"autostart", autostartEnabled()}};
    const auto    captureSettings = capture_.settings();
    for (auto it = captureSettings.cbegin(); it != captureSettings.cend(); ++it)
        result.insert(it.key(), it.value());
    result.insert(QStringLiteral("ocrStatus"), capture_.ocrStatus());
    const auto explanationSettings = catalog_.settings();
    for (auto it = explanationSettings.cbegin(); it != explanationSettings.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QVariantList AppController::words() const
{
    return wordRows(QDate::currentDate(), storage_.statsStore().history());
}

QVariantList AppController::yearDays() const
{
    return yearGrid(QDate::currentDate(), storage_.statsStore().daily());
}

QString AppController::exportFor(core::ExportScope const scope) const
{
    return QString::fromStdString(core::exportLemmas(storage_.statsStore().history(), scope));
}

QString AppController::exportJsonFor(core::ExportScope const scope) const
{
    return QString::fromStdString(core::exportLemmasJson(storage_.statsStore().history(), scope));
}

QString AppController::exportWords(QString scope)
{
    const auto which = parseScope(scope);
    if (not which) {
        LENS_WARN("export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return {};
    }
    const QString text = exportFor(*which);
    LENS_INFO("words export for '{}': {} character(s) ready", scope.toStdString(), text.size());
    return text;
}

QString AppController::exportWordsJson(QString scope)
{
    const auto which = parseScope(scope);
    if (not which) {
        LENS_WARN("json export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return {};
    }
    const QString text = exportJsonFor(*which);
    LENS_INFO("words json export for '{}': {} character(s) ready", scope.toStdString(), text.size());
    return text;
}

bool AppController::saveWords(QUrl path, QString scope)
{
    const auto which = parseScope(scope);
    if (not which) {
        LENS_WARN("words export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return false;
    }
    const QString local = path.toLocalFile();
    if (local.isEmpty()) {
        LENS_WARN("words export was given '{}', which names no local file", path.toString().toStdString());
        return false;
    }
    const QString text = isJsonPath(local) ? exportJsonFor(*which) : exportFor(*which);
    QSaveFile     file(local);
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

bool AppController::importWords(QUrl path)
{
    const QString local = path.toLocalFile();
    if (local.isEmpty()) {
        LENS_WARN("words import was given '{}', which names no local file", path.toString().toStdString());
        return false;
    }
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        LENS_WARN("words import could not open '{}' for reading", local.toStdString());
        return false;
    }
    const QByteArray data = file.readAll();

    const auto incoming = importLemmas(data, isJsonPath(local));
    if (not incoming) {
        LENS_WARN("words import: '{}' is not a JSON array", local.toStdString());
        return false;
    }

    std::unordered_set<std::string> existing;
    for (const auto& entry : storage_.statsStore().history())
        existing.insert(entry.lemma);
    const QString now  = QString::fromStdString(util::localMinuteNow());
    const auto    rows = importRows(*incoming, existing, now.toStdString());
    for (const auto& row : rows) {
        storage_.statsStore().recordPop(row.lemma, row.minute);
        storage_.statsStore().recordVerdict(row.lemma, row.minute, row.verdict);
    }
    storage_.save();
    if (not rows.empty()) {
        emit statsChanged();
        emit yearDaysChanged();
    }
    LENS_INFO("words import from '{}': {} word(s) added", local.toStdString(), rows.size());
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
    }
    return removed;
}

CostDuty* AppController::costDuty()
{
    return &cost_;
}

UpdateDuty* AppController::updateDuty()
{
    return &updateDuty_;
}

SyncDuty* AppController::syncDuty()
{
    return &sync_;
}

ExplanationDuty* AppController::explanationDuty()
{
    return &explanation_;
}

CaptureDuty* AppController::captureDuty()
{
    return &capture_;
}

GlassBackdrop* AppController::glassBackdrop()
{
    return &glass_;
}

ModelCatalogDuty* AppController::modelCatalogDuty()
{
    return &catalog_;
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

QString AppController::buildDate() const
{
    // __DATE__ is English ("Oct 12 2026", the day space-padded), so the C locale parses it whatever
    // language the reader has chosen.
    const QDate built = QLocale::c().toDate(QString::fromLatin1(__DATE__).simplified(), QStringLiteral("MMM d yyyy"));
    return built.toString(Qt::ISODate);
}

QVariantMap AppController::settingPreview() const
{
    return previewSample_;
}

void AppController::refreshCaptureGates()
{
    const bool budgetPaused = !cost_.canScan();
    capture_.setBudgetPaused(budgetPaused);
    capture_.setScanPaused(budgetPaused || !explanation_.busyLabel().isEmpty());
}

}
