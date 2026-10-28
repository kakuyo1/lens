/** @file ocr_space.h
 * @brief The one online OCR protocol of the first release, and its request/response contract.
 */
#pragma once

#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

#include "capture/ocr_engine.h"
#include "http_transport.h"

namespace lens::ocr {

struct OcrSpaceConfig {
    QUrl    endpoint;
    QString apiKey;
    QString language = QStringLiteral("eng"); ///< `eng` or the service's `auto`.
};

/// @return Why @p endpoint may not be used, or nothing when it is an acceptable HTTPS OCR URL.
std::optional<QString> validateOcrSpaceEndpoint(QUrl const& endpoint);

/// @return Why @p language is not a first-release OCR.space choice, or nothing.
std::optional<QString> validateOcrSpaceLanguage(QString const& language);

/// @brief One consented online recognition. It is not an OcrEngine: scanning has no way to reach it.
struct OnlineOcrClient {
    virtual ~OnlineOcrClient()                                                          = default;
    virtual capture::OcrResult recognize(capture::OcrRequest const&        request,
                                         std::shared_ptr<std::atomic_bool> cancel = {}) = 0;
};

/// @brief Build the adapter over @p transport; the transport is injected so tests stay offline.
std::unique_ptr<OnlineOcrClient> makeOcrSpaceClient(OcrSpaceConfig config, std::shared_ptr<HttpTransport> transport);

/// The hard upload cap, conservative against the service's 1 MB free-tier file limit.
inline constexpr qsizetype kOcrSpaceMaxUploadBytes = 950'000;

}
