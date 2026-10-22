#pragma once

/**
 * @file app_controller.h
 * @brief QML facade that wires the application duties to the surfaces.
 *
 * Capture, explanation, cost and storage behavior live behind their own boundaries. This class
 * keeps the QML singleton contract and forwards commands and projections between those duties.
 */

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <filesystem>

#include "app/duties/capture_duty.h"
#include "app/duties/capture_refusal.h"
#include "core/domain/pet/pet_state.h"
#include "core/domain/settings_table.h"
#include "app/duties/cost_duty.h"
#include "app/duties/explanation_duty.h"
#include "app/glass/glass_backdrop.h"
#include "app/platform/global_hotkey.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "app/duties/model_catalog_duty.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/duties/picture_duty.h"
#include "app/platform/tray.h"
#include "app/duties/review_duty.h"
#include "app/duties/storage_duty.h"
#include "app/duties/sync_duty.h"
#include "app/duties/update_duty.h"
#include "pictures/picture_sources/commons_source.h"

class QQmlEngine;
class QJSEngine;

namespace lens::app::pet {
class PetController;
}

namespace lens::app {

class AppController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Controller)
    QML_SINGLETON
public:
    /**
     * @brief Wire the QML facade to the application services.
     * @param store Settings and word marks, owned by main().
     * @param llm Explanation client, owned by main().
     * @param hook Desktop selection hook, owned by main().
     * @param hotkey System-wide capture trigger, owned by main().
     * @param pricing Loaded model price list, owned by main().
     * @param dataDir Directory `data/update.json` is read from, the same one `data/llm/` is.
     * @param parent QObject parent.
     */
    AppController(core::KnownStore&            store,
                  llm::LlmClient&              llm,
                  MouseSelectionHook&          hook,
                  GlobalHotkey&                hotkey,
                  const llm::Pricing&          pricing,
                  const std::filesystem::path& dataDir,
                  QObject*                     parent = nullptr);
    ~AppController() override;

    /// @brief Hand the QML engine the one instance built by the composition root.
    static void provide(AppController* instance);

    /// @brief Factory used by the QML singleton registration.
    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /**
     * @brief Take the trigger key the reader chose, or the default, on the shell's registry.
     * @return True when the shell holds it. False costs the trigger, not the program, and the
     *         settings page says so.
     * @note Separate from the constructor, and called by main() after the surfaces are up, for the
     *       same reason the mouse hook's install() is: this touches the desktop, and a run that
     *       builds surfaces -- the test tree -- must not take a key away from the machine it runs on.
     */
    bool installHotkey();

    Q_INVOKABLE QPoint cursorPos() const;
    /// @param deviceName The screen's name as QML's Screen.name gives it.
    /// @return That screen's available geometry in DIPs, or an empty rect when no screen has the name.
    Q_INVOKABLE QRect screenWorkArea(QString deviceName) const;
    /// @param executable File the reader picked in the system's dialog; C++ takes the local path.
    Q_INVOKABLE void setTesseractExecutable(QUrl executable);
    /// @param directory Folder the reader picked in the system's dialog; C++ takes the local path.
    Q_INVOKABLE void setTesseractDataDirectory(QUrl directory);
    Q_INVOKABLE void restoreCaptureDefaults();
    /**
     * @brief Put every setting the settings panel owns back to its default.
     *
     * The values are the ones the settings table carries, so a restore and a fresh install read the
     * same rows. The API key, autostart, the interface language and the daily budget are not settings
     * this panel restores and are left as they are.
     */
    Q_INVOKABLE void restoreDefaults();
    /**
     * @brief Write one setting, by the key the projection hands out.
     *
     * The one write path for every key the settings table owns: the value is brought into the row's
     * rule -- a range, a set of members, #rrggbb, a family this machine has -- and stored, and the row
     * names the signal the write raises (docs/adr/0083). A setting whose write is a command of its own
     * keeps its own entry: the API key, the service, the trigger key and the daily budget.
     *
     * @param key   The document key, as `settings` reads it.
     * @param value The value to store: a number, a switch or a string.
     * @return False when the key is not in the table, or when its row refuses the value; nothing was
     *         written then.
     */
    Q_INVOKABLE bool setSetting(QString key, QVariant value);
    /// @return The closed range a key's slider runs over, as {low, high}; empty for a key with none.
    Q_INVOKABLE QVariantMap settingRange(QString key) const;
    /**
     * @brief Switch the explanation language from the bubble's shortcut and ask again for the card.
     *
     * This is the settings row's write plus the one thing the reader meant by using it on a card:
     * the standing explanation is put up in the new language, from the cache when it has one and
     * from the model otherwise. The choice is a setting, not a one-card override, so the next card
     * comes up in the same language. A translation has no second language to ask for and is left
     * alone.
     */
    Q_INVOKABLE void reexplainIn(QString language);
    /// @return Every family on this machine that draws Latin, as DropdownField options, sorted.
    Q_INVOKABLE QVariantList latinFontFamilies() const;
    /// @return The sizes the font page offers, as DropdownField options: the default marked.
    Q_INVOKABLE QVariantList bubbleFontSizeOptions() const;
    /// @brief Store the key the service is reached with. Never logged, never echoed back.
    Q_INVOKABLE void setApiKey(QString key);
    Q_INVOKABLE void setApiUrl(QString url);
    /// @brief Store the model the service is asked for, through the catalogue that owns the choice.
    Q_INVOKABLE void setModel(QString model);
    Q_INVOKABLE void setAutostart(bool on);
    /**
     * @brief Record the trigger combination the reader pressed in the settings field.
     * @param key       Qt key code of the key that was not a modifier.
     * @param modifiers The modifiers held when it went down.
     * @return Empty once it is stored and the shell has taken it; otherwise why it was refused, for
     *         the field to say so -- "unusable" (no Ctrl or Alt, so it would take a bare key from
     *         every other program) or "taken" (another program already holds it). A refused choice
     *         leaves the combination that was in force working.
     */
    Q_INVOKABLE QString setOcrHotkey(int key, int modifiers);
    /// @brief Show the explanation already stored for a word the reader pointed at, if there is one.
    /// @param x,y Where the row is, in the physical pixels the mouse hook reports.
    Q_INVOKABLE void reviewWord(QString lemma, int x, int y);
    /// @brief Ask the model about a lemma the reader asked for by name, anchored where it was read.
    Q_INVOKABLE void explainLemma(QString lemma);
    /// @brief Capture a region the reader dragged on the mask.
    /// @return Empty once the capture is under way; otherwise the reason, which is also reported.
    Q_INVOKABLE QString captureRegion(QRect region);
    Q_INVOKABLE QString exportWords(QString scope);
    Q_INVOKABLE QString exportWordsJson(QString scope);
    Q_INVOKABLE bool    saveWords(QUrl path, QString scope);
    Q_INVOKABLE bool    importWords(QUrl path);
    Q_INVOKABLE bool    removeWord(QString lemma);
    Q_INVOKABLE bool    setDailyBudget(double amount);

    /// @note Not const, and the only projection that is not: reading a document key goes through a
    ///       section, which is a writable view. Every other dependency is borrowed by reference and
    ///       this one is owned, which is what makes the difference; see docs/adr/0024.
    QVariantMap  settings();
    QVariantList words() const;
    /// @brief The last 365 local dates, including zero-use days, for the annual words surface.
    QVariantList yearDays() const;
    /// @brief The cost duty, which QML reads as `Controller.costDuty`; the facade owns it.
    CostDuty* costDuty();
    /// @brief The update duty, which QML reads as `Controller.updateDuty`; the facade owns it.
    UpdateDuty* updateDuty();
    /// @brief The sync duty, which QML reads as `Controller.syncDuty`; the facade owns it.
    SyncDuty* syncDuty();
    /// @brief The explanation duty, which QML reads as `Controller.explanationDuty`; the facade owns it.
    ExplanationDuty* explanationDuty();
    /// @brief The capture duty, which QML reads as `Controller.captureDuty`; the facade owns it.
    CaptureDuty* captureDuty();
    /// @brief The glass backdrop, which QML reads as `Controller.glassBackdrop`; the facade owns it.
    GlassBackdrop* glassBackdrop();
    /// @brief The model catalogue, which QML reads as `Controller.modelCatalogDuty`; the facade owns it.
    ModelCatalogDuty* modelCatalogDuty();
    /// @brief The picture lookup, which QML reads as `Controller.pictureDuty`; the facade owns it.
    PictureDuty* pictureDuty();
    QString      modeLabel() const;
    QString      busyLabel() const;
    /// @return The snapshot the tray icon and its tooltip are drawn from. A projection, not state.
    TrayReadout trayReadout();
    double      dailyBudget() const;
    /// @brief The day this build was compiled, as `yyyy-MM-dd`, for the about page. Not the day it runs.
    QString buildDate() const;
    /**
     * @brief The demo card the settings preview draws, out of `data/preview.json` (UI.md 4.4).
     *
     * The keys are `word`, `ipa`, `example`, and `definition`, `etymology`, `senses` -- those
     * three carrying one entry per explanation language. QML builds a bubble payload out of it
     * (components/PreviewSample.qml); nothing here has to know what a setting is. The map is empty
     * when the file could not be read, and the preview then simply does not come up.
     */
    QVariantMap settingPreview() const;

    /// @return The word the pet is asking about now, or empty when no question is up (docs/adr/0050).
    QString reviewQuestion() const;
    /// @brief Bind the desktop pet, which the review duty asks whether it is enabled and idle. The composition root calls it once.
    void bindPet(pet::PetController* pet);
    /// @brief Answer the question up now. The stored explanation then shows as a bubble beside the sign at (x, y).
    Q_INVOKABLE void answerReview(bool remembered, int x, int y);
    /// @brief Set how often the pet checks for a due word. Only the check changes; no word's curve moves.
    Q_INVOKABLE void setReviewFrequency(int value);

    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)
    Q_PROPERTY(QVariantList yearDays READ yearDays NOTIFY yearDaysChanged)
    Q_PROPERTY(lens::app::CostDuty* costDuty READ costDuty CONSTANT)
    Q_PROPERTY(lens::app::UpdateDuty* updateDuty READ updateDuty CONSTANT)
    Q_PROPERTY(lens::app::SyncDuty* syncDuty READ syncDuty CONSTANT)
    Q_PROPERTY(lens::app::ExplanationDuty* explanationDuty READ explanationDuty CONSTANT)
    Q_PROPERTY(lens::app::CaptureDuty* captureDuty READ captureDuty CONSTANT)
    Q_PROPERTY(lens::app::GlassBackdrop* glassBackdrop READ glassBackdrop CONSTANT)
    Q_PROPERTY(lens::app::ModelCatalogDuty* modelCatalogDuty READ modelCatalogDuty CONSTANT)
    Q_PROPERTY(lens::app::PictureDuty* pictureDuty READ pictureDuty CONSTANT)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    Q_PROPERTY(double dailyBudget READ dailyBudget NOTIFY dailyBudgetChanged)
    Q_PROPERTY(QString buildDate READ buildDate CONSTANT)
    Q_PROPERTY(QVariantMap settingPreview READ settingPreview CONSTANT)
    Q_PROPERTY(QString reviewQuestion READ reviewQuestion NOTIFY reviewQuestionChanged)

signals:
    void selectionBarRequested(QVariantMap payload);
    /// @brief An event the desktop pet reacts to, forwarded from the duties that raise it. Carries no text.
    void petEvent(lens::core::pet::PetEvent event);
    void pointerPressed(QPoint at);
    /// @brief The trigger key was pressed and OCR can run: put the capture mask up.
    void captureMaskRequested();
    void settingsChanged();
    void statsChanged();
    void yearDaysChanged();
    void busyChanged();
    void uiLanguageChanged(QString lang);
    void dailyBudgetChanged();
    /// @brief The question went up or came down; reviewQuestion() holds the current one.
    void reviewQuestionChanged();

private:
    /// @brief Write one row: the value through the row's rule, then the signal the row names.
    bool writeSetting(core::settings::Row const& row, QVariant const& value);
    /// @brief Restore provider wire options and persisted connection choices without touching credentials.
    void restoreModelService();
    /// @brief The plain-text export for a scope, without the QML string parse.
    QString exportFor(core::ExportScope scope) const;
    /// @brief The JSON export for a scope, without the QML string parse.
    QString exportJsonFor(core::ExportScope scope) const;
    /// @brief Reconcile budget and in-flight request gates with the capture duty.
    void refreshCaptureGates();
    /// @brief The trigger key went down: raise the mask, or say why it cannot be raised.
    void onTriggerHotkey();
    /// @brief Report a capture that will not start, in the reader's words rather than a code.
    void reportCaptureRefusal(CaptureRefusal refusal);

    static AppController* instance_;

    StorageDuty            storage_;
    CostDuty               cost_;
    CaptureDuty            capture_;
    GlassBackdrop          glass_;
    ModelCatalogDuty       catalog_;
    ExplanationDuty        explanation_;
    update::UpdateClient   updateClient_;
    update::DownloadClient updateDownloader_;
    UpdateDuty             updateDuty_;
    SyncDuty               sync_;
    ReviewDuty             reviewDuty_;
    /// The picture lookup: the source that searches, the cache beside the settings document, and the
    /// duty the card reads. Declared in this order so the duty's two references are alive before it.
    pictures::CommonsSource picturesSource_;
    pictures::PictureCache  picturesCache_;
    PictureDuty             pictureDuty_;
    llm::LlmClient&         llm_;
    GlobalHotkey&           hotkey_;
    QTimer                  gateTimer_;
    /// Whether the daily budget was spent the last time the cost duty reported, so a pause and a resume each fire once.
    bool budgetPaused_;
    /// The settings preview's demo card, read once (settingPreview()).
    QVariantMap previewSample_;
    /// The desktop pet, bound by the composition root. The review duty asks it whether it may take a question.
    pet::PetController* pet_{nullptr};
};

}
