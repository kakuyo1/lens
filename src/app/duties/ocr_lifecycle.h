#pragma once

/**
 * @file ocr_lifecycle.h
 * @brief The OCR engines' life: the chosen local engine and its readiness, the Tesseract engine the
 *        scan reads with, the settings-to-configuration mapping, and the test and detect state the
 *        settings pages project -- the policy the capture duty's engine block carried, in one module
 *        (docs/adr/0165).
 *
 * Value in, value out: no QObject, no clock. The jobs the engines answer run on the duty's executor
 * -- the duty submits them and routes the results back here as state changes. A rebuild cancels the
 * job in flight through the callable the duty hands in: the answer would land on the engine that
 * just went away.
 */

#include <QString>

#include <functional>
#include <memory>

#include "capture/ocr_engine.h"
#include "capture/ocr_local.h"
#include "capture_refusal.h"
#include "storage_duty.h"

namespace lens::app {

/// @return The Tesseract the reader pointed at, or the bundled runtime when both are empty.
capture::TesseractConfig storedTesseractConfig(StorageDuty& storage);
/// @return The RapidOCR the reader pointed at, with the trust record the profile holds.
capture::RapidOcrConfig storedRapidOcrConfig(StorageDuty& storage);
/// @return The local configuration as the document holds it: engine, language, both engines' paths.
capture::LocalOcrConfig storedLocalConfig(StorageDuty& storage);

/// @return The diagnostic token for an OCR status, including the ready state.
QString statusToken(capture::OcrStatus status);
/// @return The token for a reason, as the QString the QML boundary reads.
QString tokenOf(CaptureRefusal refusal);

class OcrLifecycle {
public:
    /// @param storage The document the paths and the trust record are read from; outlives *this.
    /// @param cancelJobs Cancels the job in flight and empties the duty's job slot; a rebuild calls
    ///                   it before the engine it replaces is released.
    OcrLifecycle(StorageDuty& storage, std::function<void()> cancelJobs);

    /// @brief The chosen engine is already built -- the test constructors inject one -- and only its
    ///        readiness is inspected.
    void adopt(std::unique_ptr<capture::OcrEngine> engine, capture::LocalOcrConfig const& local);
    /// @brief Rebuild the chosen local engine around the stored paths and inspect the new one: the
    ///        job in flight dies with the engine, and the test state resets.
    void rebuild();
    /// @brief Write the local policy for a document that predates ocrConfigVersion.
    void migrate();

    /// @brief A probe answered; the engine's recorded status follows it.
    void setStatus(capture::OcrStatus status);
    /// @brief A test started: the page reads `checking` until it answers.
    void testStarted();
    /// @brief A test could not start, in the token the settings page explains.
    void testRefused(QString token);
    /// @brief A test answered; the page spells what came back.
    void testFinished(capture::OcrResult const& result);
    /// @brief A test in flight dies with the configuration that asked for it.
    void testCancelled();
    /// @brief The RapidOCR detection returns to idle: the trust record moved under it.
    void detectIdle();
    /// @brief A detection started: the page reads `checking` until it answers.
    void detectStarted();
    /// @brief A detection was refused before it ran.
    void detectRefused(capture::OcrStatus status);
    /// @brief A detection ended in @p state, in the token the settings page explains.
    void detectDone(QString state, int ms);

    [[nodiscard]] std::shared_ptr<capture::OcrEngine> const& engine() const
    {
        return ocr_;
    }
    [[nodiscard]] capture::OcrStatus status() const
    {
        return ocrState_;
    }
    [[nodiscard]] capture::LocalOcrInspection const& inspection() const
    {
        return localInspection_;
    }
    [[nodiscard]] QString testState() const
    {
        return ocrTestState_;
    }
    [[nodiscard]] QString testReason() const
    {
        return ocrTestReason_;
    }
    [[nodiscard]] QString testEngine() const
    {
        return ocrTestEngine_;
    }
    [[nodiscard]] QString testLanguage() const
    {
        return ocrTestLanguage_;
    }
    [[nodiscard]] int testMs() const
    {
        return ocrTestMs_;
    }
    [[nodiscard]] QString detectState() const
    {
        return rapidOcrDetectState_;
    }
    [[nodiscard]] int detectMs() const
    {
        return rapidOcrDetectMs_;
    }

private:
    /// @brief Readiness is a file/resource inspection; no child process is started until the reader
    ///        asks for a real test or captures a region. RapidOCR's digest is verified here, at
    ///        startup and after a path or language change, rather than on every settings projection.
    void inspect(capture::LocalOcrConfig const& local);

    StorageDuty&                        storage_;
    std::function<void()>               cancelJobs_;
    std::shared_ptr<capture::OcrEngine> ocr_; ///< The chosen local engine, for the screenshot path.
    capture::OcrStatus                  ocrState_ = capture::OcrStatus::RuntimeMissing;
    capture::LocalOcrInspection         localInspection_;
    QString                             ocrTestState_ = QStringLiteral("idle");
    /// Why the last test failed, in the vocabulary the settings page explains; `none` otherwise.
    QString ocrTestReason_ = QStringLiteral("none");
    QString ocrTestEngine_;
    QString ocrTestLanguage_;
    int     ocrTestMs_           = 0;
    QString rapidOcrDetectState_ = QStringLiteral("idle");
    int     rapidOcrDetectMs_    = 0;
};

}
