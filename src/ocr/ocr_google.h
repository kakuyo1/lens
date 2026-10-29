/** @file ocr_google.h
 * @brief Google Cloud Vision text detection: one JSON call, keyed by an API key.
 */
#pragma once

#include <QString>
#include <QUrl>

#include <memory>

#include "http_transport.h"
#include "online_ocr.h"

namespace lens::ocr {

struct GoogleConfig {
    QUrl    endpoint;
    QString apiKey;
    QString language; ///< `en` adds an English hint; empty lets the service detect.
};

/// @brief Build the adapter over @p transport; the key travels in a header, never in the URL.
std::unique_ptr<OnlineOcrClient> makeGoogleClient(GoogleConfig config, std::shared_ptr<HttpTransport> transport);

/// The service's own caps: the image, and the JSON request that carries it base64-encoded.
inline constexpr qsizetype kGoogleMaxImageBytes   = 20'000'000;
inline constexpr qsizetype kGoogleMaxRequestBytes = 10'000'000;

}
