/**
 * @file upload_transaction.cpp
 * @brief The upload transaction's state machine, moved verbatim from the capture duty's upload block
 *        (docs/adr/0165).
 */

#include "upload_transaction.h"

#include <algorithm>

namespace lens::app {

UploadTransaction::UploadTransaction(std::shared_ptr<ocr::HttpTransport> transport, std::shared_ptr<ocr::OcrTokenStore> tokens)
    : transport_(std::move(transport)), tokens_(std::move(tokens))
{}

UploadStep UploadTransaction::begin(QImage image, QPoint anchor, ocr::OnlineOcrConfig config, std::uint64_t const requestId, std::chrono::steady_clock::time_point const now)
{
    UploadStep step;
    // A transaction already standing is replaced without announcing itself: the new consent card is
    // the announcement.
    if (upload_) step.changed = true;
    upload_.reset();
    phase_    = QStringLiteral("idle");
    canRetry_ = false;

    Pending pending;
    pending.requestId        = requestId;
    pending.image            = std::move(image);
    pending.anchor           = anchor;
    pending.config           = std::move(config);
    pending.absoluteDeadline = now + kAbsoluteWindow;
    upload_                  = std::move(pending);
    phase_                   = QStringLiteral("awaiting-consent");
    canRetry_                = false;
    step.kind                = UploadStep::Kind::Consent;
    step.card                = {requestId, upload_->config.endpoint.host(), upload_->config.provider.label, upload_->config.language, kConsentWindow.count()};
    step.image               = upload_->image;
    step.changed             = true;
    return step;
}

UploadStep UploadTransaction::confirm(qulonglong const requestId, std::uint64_t const screenshotGeneration, std::chrono::steady_clock::time_point const now)
{
    if (not upload_ or upload_->requestId != requestId) return {};
    if (now >= upload_->absoluteDeadline) return clear(QStringLiteral("expired"), true);
    if (upload_->attempts >= kAttemptCap) return clear(QStringLiteral("retry-limit"), true);
    std::shared_ptr<ocr::OnlineOcrClient> client{ocr::makeOnlineOcrClient(upload_->config, transport_, tokens_)};
    if (not client) return clear(QStringLiteral("configuration-changed"), true);

    auto const          remaining = std::chrono::duration_cast<std::chrono::milliseconds>(upload_->absoluteDeadline - now);
    auto const          budget    = std::min(kJobBudget, remaining);
    capture::OcrRequest request{.requestId = upload_->requestId,
                                .source    = capture::OcrSource::Screenshot,
                                .image     = upload_->image,
                                .language  = std::nullopt,
                                .layout    = capture::OcrLayoutHint::Unknown,
                                .deadline  = budget};
    ++upload_->attempts;
    phase_    = QStringLiteral("uploading");
    canRetry_ = false;
    UploadStep step;
    step.kind       = UploadStep::Kind::Submit;
    step.client     = std::move(client);
    step.request    = std::move(request);
    step.anchor     = upload_->anchor;
    step.generation = screenshotGeneration;
    step.changed    = true;
    return step;
}

UploadStep UploadTransaction::submitRefused()
{
    if (not upload_) return {};
    UploadStep step;
    phase_           = QStringLiteral("awaiting-retry");
    canRetry_        = true;
    step.kind        = UploadStep::Kind::Finished;
    step.outcome     = {QStringLiteral("busy"), upload_->requestId, {}, true};
    step.retryWindow = kRetryWindow;
    step.announced   = true;
    step.changed     = true;
    return step;
}

UploadStep UploadTransaction::finish(capture::OcrResult const& result, std::chrono::steady_clock::time_point const now)
{
    if (not upload_) return {};
    auto const requestId = upload_->requestId;
    auto const anchor    = upload_->anchor;
    auto const attempts  = upload_->attempts;
    auto const absolute  = upload_->absoluteDeadline;
    auto const refusal   = refusalOf(result.status);
    auto const token     = refusal ? QString::fromLatin1(refusalKey(*refusal)) : QStringLiteral("ready");

    if (result.status == capture::OcrStatus::Ready) {
        auto const text = result.text;
        UploadStep step = clear(QStringLiteral("ready"), false);
        step.kind       = UploadStep::Kind::Completed;
        step.outcome    = {token, requestId, result.actualEngine, false};
        step.announced  = true;
        step.text       = text;
        step.anchor     = anchor;
        return step;
    }
    if (result.status == capture::OcrStatus::Cancelled) return clear(QStringLiteral("cancelled"), true);
    bool const retryable = (result.status == capture::OcrStatus::TimedOut or
                            result.status == capture::OcrStatus::TransportFailed) and
                           attempts < kAttemptCap and now < absolute and
                           result.uploadState != capture::OcrUploadState::NotUploaded;
    if (retryable) {
        UploadStep step;
        step.kind        = UploadStep::Kind::Finished;
        phase_           = QStringLiteral("awaiting-retry");
        canRetry_        = true;
        step.outcome     = {token, requestId, {}, true};
        step.retryWindow = kRetryWindow;
        step.announced   = true;
        step.changed     = true;
        return step;
    }
    return clear(token, true);
}

UploadStep UploadTransaction::consentTimedOut()
{
    if (not upload_) return {};
    if (phase_ == QLatin1String("awaiting-consent")) return clear(QStringLiteral("consent-expired"), true);
    if (phase_ == QLatin1String("awaiting-retry")) return clear(QStringLiteral("retry-expired"), true);
    return {};
}

UploadStep UploadTransaction::absoluteTimedOut()
{
    if (not upload_) return {};
    return clear(QStringLiteral("expired"), true);
}

UploadStep UploadTransaction::cancel()
{
    return clear(QStringLiteral("cancelled"), true);
}

UploadStep UploadTransaction::clear(QString outcome, bool const notify)
{
    UploadStep step;
    auto const requestId = upload_ ? upload_->requestId : 0;
    auto const hadUpload = upload_.has_value();
    upload_.reset();
    phase_       = QStringLiteral("idle");
    canRetry_    = false;
    step.cleared = hadUpload;
    step.changed = hadUpload;
    if (notify and hadUpload) {
        step.kind      = UploadStep::Kind::Finished;
        step.outcome   = {std::move(outcome), requestId, {}, false};
        step.announced = true;
    }
    return step;
}

}
