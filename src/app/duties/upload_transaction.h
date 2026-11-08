#pragma once

/**
 * @file upload_transaction.h
 * @brief The online upload transaction: the frozen crop, the reader's consent, the send and the
 *        retry decision -- the policy the capture duty's upload block carried, in one module
 *        (docs/adr/0165).
 *
 * Value in, value out: no QObject, no clock, no network. The duty reads the configured service,
 * hands the configuration in frozen, and executes the step every event returns: the consent and
 * deadline timers, the executor job and the signals stay with the duty. The configuration a
 * request goes out under is frozen when the crop is, and never re-read afterwards.
 */

#include <QImage>
#include <QPoint>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "capture/ocr_engine.h"
#include "capture_refusal.h"
#include "ocr/online_ocr.h"

namespace lens::app {

/// @brief The consent card's facts, before they are spelled into the payload QML reads.
struct UploadConsentCard {
    std::uint64_t requestId = 0;
    QString       host;     ///< The host the request would go to.
    QString       provider; ///< The provider's label, as the card shows it.
    QString       language;
    qint64        expiresInMs = 0;
};

/// @brief How a transaction ended, or what decision it wants next.
struct UploadOutcome {
    QString       status; ///< The token the surface explains: ready, expired, cancelled, busy, ...
    std::uint64_t requestId = 0;
    QString       engine; ///< The engine that answered, for the ready outcome.
    bool          canRetry = false;
};

/// @brief What one upload event asks the adapter to do.
struct UploadStep {
    enum class Kind : std::uint8_t {
        None,      ///< Nothing the reader can see happened.
        Consent,   ///< A frozen crop waits: raise @p card with @p image as its preview.
        Submit,    ///< Send @p request through @p client; @p anchor and @p generation go in the job slot.
        Finished,  ///< The transaction ended, or wants a fresh decision: @p outcome.
        Completed, ///< The answer came back: announce @p outcome, then classify @p text at @p anchor.
    } kind = Kind::None;

    UploadConsentCard                     card;           ///< Consent
    QImage                                image;          ///< Consent: the frozen crop, for the controlled preview source.
    std::shared_ptr<ocr::OnlineOcrClient> client;         ///< Submit
    capture::OcrRequest                   request;        ///< Submit
    QPoint                                anchor{};       ///< Submit, Completed
    std::uint64_t                         generation = 0; ///< Submit: the screenshot generation the job froze under.
    QString                               text;           ///< Completed: what the service read.
    UploadOutcome                         outcome;        ///< Finished, Completed
    /// Set when the consent clock restarts for a retry; a Finished without one is terminal.
    std::chrono::milliseconds retryWindow{};
    /// The outcome is the reader's to see: the duty announces it.
    bool announced = false;
    /// An upload existed and was dropped: the duty cancels an online job in its slot and
    /// invalidates the scan.
    bool cleared = false;
    /// The settings projection moved: the duty emits settingsChanged.
    bool changed = false;
};

class UploadTransaction {
public:
    /// Cadences and limits, as the transaction has always run them; the adapter arms the clocks.
    static constexpr std::chrono::milliseconds kConsentWindow{60000};
    static constexpr std::chrono::milliseconds kAbsoluteWindow{120000};
    static constexpr std::chrono::milliseconds kRetryWindow{30000};

    /// @param transport The transport every client reads; outlives *this.
    /// @param tokens The token store every client reads; outlives *this.
    UploadTransaction(std::shared_ptr<ocr::HttpTransport> transport, std::shared_ptr<ocr::OcrTokenStore> tokens);

    /// @brief A framed capture takes the online road: freeze the crop and ask for consent.
    /// @param config The frozen configuration the request will go out under.
    /// @param requestId The id the duty's issuer hands out; only spent when consent is asked.
    UploadStep begin(QImage image, QPoint anchor, ocr::OnlineOcrConfig config, std::uint64_t requestId, std::chrono::steady_clock::time_point now);
    /// @brief The reader confirmed the frozen crop for @p requestId; this is the only upload gate.
    /// @param screenshotGeneration The generation a job freezes under, for the fate comparison.
    /// @return Submit to start, or the Finished answer when the gate refused.
    UploadStep confirm(qulonglong requestId, std::uint64_t screenshotGeneration, std::chrono::steady_clock::time_point now);
    /// @brief The executor refused the job: the reader may retry within the consent clock.
    UploadStep submitRefused();
    /// @brief The executor answered for the request in flight.
    UploadStep finish(capture::OcrResult const& result, std::chrono::steady_clock::time_point now);
    /// @brief The consent or retry window ran out.
    UploadStep consentTimedOut();
    /// @brief The absolute deadline ran out; the duty cancels the job in flight.
    UploadStep absoluteTimedOut();
    /// @brief The reader dropped the transaction.
    UploadStep cancel();
    /// @brief Drop the transaction, naming @p outcome; the duty routes the step's cleared flag.
    UploadStep clear(QString outcome, bool notify);

    /// @brief Whether a crop or a request is on its way; a new transaction may not start while one is.
    [[nodiscard]] bool active() const
    {
        return upload_.has_value();
    }
    /// @brief The phase, in the vocabulary the settings projection spells.
    [[nodiscard]] QString phase() const
    {
        return phase_;
    }
    [[nodiscard]] bool canRetry() const
    {
        return canRetry_;
    }

private:
    /// @brief The transaction's state, frozen with the crop.
    struct Pending {
        std::uint64_t                         requestId = 0;
        QImage                                image;
        QPoint                                anchor;
        ocr::OnlineOcrConfig                  config; ///< Frozen at consent time; never re-read later.
        std::chrono::steady_clock::time_point absoluteDeadline;
        int                                   attempts = 0;
    };

    std::shared_ptr<ocr::HttpTransport> transport_;
    std::shared_ptr<ocr::OcrTokenStore> tokens_;
    std::optional<Pending>              upload_;
    QString                             phase_    = QStringLiteral("idle");
    bool                                canRetry_ = false;

    static constexpr std::chrono::milliseconds kJobBudget{30000};
    static constexpr int                       kAttemptCap = 2;
};

}
