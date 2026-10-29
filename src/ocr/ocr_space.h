/** @file ocr_space.h
 * @brief The OCR.space protocol: one multipart request, and its own response mapping.
 */
#pragma once

#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

#include "capture/ocr_engine.h"
#include "http_transport.h"
#include "online_ocr.h"

namespace lens::ocr {

struct OcrSpaceConfig {
    QUrl    endpoint;
    QString apiKey;
    QString language = QStringLiteral("eng"); ///< `eng` or the service's `auto`.
};

/// @return Why @p language is not a first-release OCR.space choice, or nothing.
std::optional<QString> validateOcrSpaceLanguage(QString const& language);

/// @brief Build the adapter over @p transport; the transport is injected so tests stay offline.
std::unique_ptr<OnlineOcrClient> makeOcrSpaceClient(OcrSpaceConfig config, std::shared_ptr<HttpTransport> transport);

/// The hard upload cap, conservative against the service's 1 MB free-tier file limit.
inline constexpr qsizetype kOcrSpaceMaxUploadBytes = 950'000;

}
