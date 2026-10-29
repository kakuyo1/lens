#pragma once

/**
 * @file capture_duty.h
 * @brief Selection capture and local OCR acquisition, independent of explanation presentation.
 */

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include "capture/ocr_engine.h"
#include "capture/ocr_executor.h"
#include "capture/ocr_local.h"
#include "capture/scan_pipeline.h"
#include "capture_refusal.h"
#include "app/platform/selection_text_grabber.h"
#include "core/domain/filter_core.h"
#include "ocr/online_ocr.h"
#include "storage_duty.h"

namespace lens::app {

class MouseSelectionHook;

struct PendingSelection {
    QPoint anchor;
    /// @brief The anchor's own size, for a card placed against a whole object rather than a point.
    ///        The word list's review bubble is placed above the word's rectangle, not at its corner.
    QSize   anchorSize;
    QString text;
    /// What the selection is, carried as the classifier's own kind: a value everywhere in C++, and a
    /// string only where a QML payload needs one (kindName()).
    core::SelectionKind kind = core::SelectionKind::Word;
    QString             surface;
    QString             lemma;
    QString             preset = QStringLiteral("default");
    /// @brief Whether this request is a step of the definition chain. Its answer goes onto the chain under
    ///        the card, and never onto the card itself.
    bool chain = false;
    /// @brief For a chain step, the word the card stands on. A step answers only while that is still the word
    ///        showing, so a step asked from a card the reader has since moved on from is dropped.
    QString parent;
};

/// @return The kind's one spelling on the QML wire, as the selection-bar and bubble payloads carry it.
QString kindName(core::SelectionKind kind);

/// @return Whether a selection is a word: the one kind the explanation cache, the sense modes and
///         the word's origin are about.
inline bool isWordKind(core::SelectionKind const kind)
{
    return kind == core::SelectionKind::Word;
}

/// @brief The preset a sentence selection carries once the reader asks for its breakdown. It is part
///        of the card's identity, so a breakdown and a translation or explanation of one text stay apart.
inline QString const kBreakdownPreset{QStringLiteral("breakdown")};
/// @brief The presets a spelling and a grammar correction carry, for the same reason as the breakdown's.
inline QString const kSpellPreset{QStringLiteral("spell")};
inline QString const kGrammarPreset{QStringLiteral("grammar")};

class CaptureDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.captureDuty")
public:
    CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent = nullptr);
    CaptureDuty(StorageDuty&                        storage,
                MouseSelectionHook&                 hook,
                std::unique_ptr<capture::OcrEngine> ocr,
                QObject*                            parent = nullptr);
    CaptureDuty(StorageDuty&                        storage,
                MouseSelectionHook&                 hook,
                std::unique_ptr<capture::OcrEngine> ocr,
                std::shared_ptr<ocr::HttpTransport> onlineTransport,
                QObject*                            parent = nullptr);
    ~CaptureDuty() override;

    QVariantMap settings() const;
    QString     ocrStatus() const;
    /**
     * @brief Write one of the capture settings, by the key the settings table names.
     *
     * The facade forwards here for every row whose signal is Signal::Capture, because the write is
     * more than a value: switching capture on bumps the screenshot generation, and the scan and the
     * OCR engine are rebuilt around the new number or path.
     *
     * @param key   The document key; one of the rows the table marks Signal::Capture.
     * @return False when the key is not one of this duty's, or when its value was refused.
     */
    bool             setSetting(std::string_view key, QVariant const& value);
    Q_INVOKABLE void setOcrCapture(bool on);
    Q_INVOKABLE void setAutoScan(bool on);
    Q_INVOKABLE void setMinimumWordLength(int length);
    Q_INVOKABLE bool setScanWhitelist(QString processes);
    /// @brief Point OCR at the reader's own Tesseract and probe it again; empty means the bundled one.
    Q_INVOKABLE void setTesseractExecutable(QString executable);
    /// @brief Point OCR at the reader's own traineddata directory; empty means the one beside the executable.
    Q_INVOKABLE void setTesseractDataDirectory(QString directory);
    /// @brief Point the user-installed RapidOCR at its executable; clears the trust record.
    Q_INVOKABLE void setRapidOcrExecutable(QString executable);
    /// @brief Point RapidOCR at its model folder; the path must be ASCII; clears the trust record.
    Q_INVOKABLE void setRapidOcrModelsDirectory(QString directory);
    /// @brief Run one synthetic image through RapidOCR and record its trust digest on success.
    Q_INVOKABLE void detectRapidOcr();
    /// @brief Choose how a local capture picks its engine: Auto, Tesseract or RapidOCR.
    Q_INVOKABLE void setOcrLocalEngine(QString engine);
    /// @brief Choose the language the local engine reads: en, zh-Hans or zh-Hans+en.
    Q_INVOKABLE void setOcrLocalLanguage(QString language);
    /// @brief Choose whether a manual capture stays local or goes online.
    Q_INVOKABLE void setOcrMode(QString mode);
    /// @brief Set the online provider id; empty clears it. Changing it drops the address and credentials.
    Q_INVOKABLE void setOcrOnlineProvider(QString provider);
    /// @brief Set an HTTPS override of the provider's address; empty restores the provider's own.
    Q_INVOKABLE void setOcrOnlineEndpoint(QString endpoint);
    /// @brief Set the online recognition language: `eng` or `auto`.
    Q_INVOKABLE void setOcrOnlineLanguage(QString language);
    /// @brief Store one credential the chosen provider names; empty clears that slot.
    Q_INVOKABLE void setOcrCredential(QString slot, QString value);
    /// @brief The reader confirmed the frozen crop for @p requestId; this is the only upload gate.
    Q_INVOKABLE void confirmOnlineUpload(qulonglong requestId);
    /// @brief Drop a pending or running online request and its image.
    Q_INVOKABLE void cancelOnlineUpload();
    /// @brief Re-read the online configuration after a credential change and drop any consent.
    /// @note The caller announces the setting change; an upload this clears announces itself.
    void invalidateOnlineConfiguration();
    /// @brief Put every row the table marks Signal::Capture back to its default.
    Q_INVOKABLE void restoreDefaults();
    /// @brief Recognize a synthetic image with the configured local engine; no desktop is read.
    Q_INVOKABLE void testLocalOcr();
    /// @brief Send one synthetic image to the configured online service; this may be billed.
    Q_INVOKABLE void testOnlineOcr();

    /// @brief Pause while a request is in flight.
    void setScanPaused(bool paused);
    /// @brief Pause every acquisition path when the daily budget is exhausted.
    void setBudgetPaused(bool paused);
    /// @return False when OCR is disabled, unavailable, busy, or budget-paused.
    bool captureScreenshot(QImage image, QPoint anchor);
    /// @brief Capture a reader-selected logical region wholly inside one screen.
    /// @param region The dragged rectangle, in the device-independent desktop coordinates
    ///               QScreen geometry uses.
    /// @return Nothing once the capture is under way; otherwise the reason it is not, which is
    ///         what the surface that asked reports to the reader. A trigger that does nothing at
    ///         all is the one outcome that cannot be told from a broken one.
    std::optional<CaptureRefusal> captureRegion(QRect region);

    /// @brief Why a capture cannot start this moment, before any pixels are read.
    /// @return Nothing when it can, otherwise the reason; CaptureRefusal is the set.
    /// @note The one gate both the screenshot and the region path go through, so a reader who is
    ///       told why hears the same reason whichever of the two asked.
    std::optional<CaptureRefusal> captureRefusalReason() const;
    void                          onSelectionReleased(QPoint anchor);
    /// @brief Start the selection pipeline at @p anchor.
    /// @param anchor Release position, in the physical pixels the mouse hook reports.
    /// @param text The selected text, when the caller already has it. Given, the clipboard is not read: the developer
    ///        surface (docs/adr/0068) drives the pipeline this way, with no foreground application to copy from. The
    ///        mouse path never passes it.
    void beginSelection(QPoint anchor, std::optional<QString> text = std::nullopt);

signals:
    void selectionBarRequested(QVariantMap payload);
    /// @brief Text that arrived with its action already chosen -- the screenshot path, which asks
    ///        for the translation itself rather than for the bar.
    void selectionActionRequested(QString action, QString text);
    void selectionReady(PendingSelection selection);
    void pointerPressed(QPoint at);
    void settingsChanged();
    /// @brief Only filtered candidates, never an OCR snapshot, leave this duty.
    void scanCandidatesReady(QVariantList candidates, QPoint anchor);
    /// @brief Count-only report for entity-shaped text unsupported by the scan channel.
    void scanEntitiesUnsupported(int count);
    /// @brief A frozen crop is waiting for the reader's explicit upload decision.
    void uploadConsentRequested(QVariantMap consent);
    /// @brief The frozen crop itself, for the confirmation card's controlled image source.
    void uploadPreviewReady(qulonglong requestId, QImage image);
    /// @brief The upload transaction ended, or needs a fresh manual retry decision.
    void uploadFinished(QVariantMap outcome);
    /// @brief A framed region produced no text, or an image OCR could not read.
    ///
    /// Only the framed region, and only the reason: a scan that hiccups is not worth a card, and
    /// `ocrStatus()` is where the diagnostic spelling of the same state is read from.
    void screenshotFailed(CaptureRefusal refusal);

private:
    struct OcrJob {
        QPoint        anchor;
        QString       context;
        std::uint64_t generation  = 0;
        std::uint64_t requestId   = 0;
        bool          probe       = false;
        bool          test        = false;
        bool          online      = false;
        bool          detectRapid = false;
    };

    struct PendingUpload {
        std::uint64_t                         requestId = 0;
        QImage                                image;
        QPoint                                anchor;
        ocr::OnlineOcrConfig                  config; ///< Frozen at consent time; never re-read later.
        std::chrono::steady_clock::time_point absoluteDeadline;
        int                                   attempts = 0;
    };

    /// @brief Rebuild the OCR engine around the stored paths and inspect the new one.
    void rebuildOcr();
    /// @brief Run the injected engine's readiness probe; production readiness is file inspection.
    void probeOcr();
    /// @brief Write the local policy for a document that predates ocrConfigVersion.
    void migrateOcrSettings();
    /// @param fromScreenshot True when the text came out of a framed region rather than a
    ///        selection. A screenshot is the reader saying "translate this": they framed it with a
    ///        gesture of their own, so the bar would ask a question they have already answered. A
    ///        selection keeps the bar -- there the gesture only says "show me what this says", and
    ///        the bar is where they say what to do with it.
    void classifyText(QString text, QPoint anchor, bool fromScreenshot = false);
    void scanTick();
    void onOcrFinished(std::uint64_t requestId, capture::OcrResult result);
    bool launchOcr(QImage image, QPoint anchor, QString context);
    void beginOnlineUpload(QImage image, QPoint anchor);
    void onConsentTimeout();
    void onUploadTimeout();
    void finishOnline(capture::OcrResult const& result);
    void clearUpload(QString outcome, bool notify);
    bool onlineConfigured() const;
    /// @return Whether the chosen service reads @p language; before a service is chosen, the two
    ///         values every service in the table lists.
    bool onlineLanguageSupported(QString const& language) const;
    /// @return The provider the reader chose, or null while none is.
    ocr::OnlineOcrProvider const* onlineProvider() const;
    /// @return The address a request would go to: the reader's override, else the provider's own.
    QUrl onlineEndpoint(ocr::OnlineOcrProvider const& provider) const;
    /// @return Everything a client needs, frozen from the document; empty when no provider is set.
    ocr::OnlineOcrConfig onlineConfig() const;
    QString              onlineKey() const;
    QString              onlineSecret() const;
    /// @brief Forget exchanged tokens; their key or address changed, or the reader cleared them.
    void        clearOnlineTokens();
    void        invalidateScan();
    void        publishBatch(capture::ScanTime now, QPoint anchor);
    std::size_t minFreqRank() const;

    StorageDuty&                          storage_;
    MouseSelectionHook&                   hook_;
    std::unique_ptr<SelectionTextGrabber> grabber_;
    std::shared_ptr<capture::OcrEngine>   ocr_;
    std::unique_ptr<capture::OcrExecutor> executor_;
    std::shared_ptr<ocr::HttpTransport>   onlineTransport_;
    /// Tokens a service exchanged this run. Memory only; cleared whenever the key or address moves.
    std::shared_ptr<ocr::OcrTokenStore>    onlineTokens_;
    std::unique_ptr<capture::ScanPipeline> scan_;
    QTimer                                 scanTimer_;
    std::optional<OcrJob>                  jobContext_;
    std::optional<PendingUpload>           upload_;
    QTimer                                 consentTimer_;
    QTimer                                 uploadTimer_;
    QString                                uploadPhase_          = QStringLiteral("idle");
    bool                                   uploadCanRetry_       = false;
    capture::OcrStatus                     ocrState_             = capture::OcrStatus::RuntimeMissing;
    std::uint64_t                          generation_           = 0;
    std::uint64_t                          screenshotGeneration_ = 0;
    std::uint64_t                          nextRequestId_        = 0;
    QString                                scanContext_;
    QPoint                                 scanAnchor_;
    bool                                   scanPaused_   = false;
    bool                                   budgetPaused_ = false;
    QString                                ocrTestState_ = QStringLiteral("idle");
    QString                                ocrTestEngine_;
    QString                                ocrTestLanguage_;
    int                                    ocrTestMs_ = 0;
    capture::LocalOcrInspection            localInspection_;
    QString                                rapidOcrDetectState_ = QStringLiteral("idle");
    int                                    rapidOcrDetectMs_    = 0;
};

}
