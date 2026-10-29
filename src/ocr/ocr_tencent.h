/** @file ocr_tencent.h
 * @brief Tencent Cloud OCR (GeneralBasicOCR), signed with TC3-HMAC-SHA256.
 */
#pragma once

#include <QString>
#include <QUrl>

#include <memory>

#include "http_transport.h"
#include "online_ocr.h"

namespace lens::ocr {

struct TencentConfig {
    QUrl    endpoint; ///< `https://ocr.tencentcloudapi.com/`; the host is part of the signature.
    QString secretId;
    QString secretKey;
    QString language; ///< `en` or `auto`, the service's own `LanguageType` values.
};

/**
 * @brief The Authorization header for one TC3-signed request.
 *
 * Exposed rather than buried in the request builder so a published example can be checked without
 * a socket; the timestamp is a parameter for the same reason.
 */
QString tc3Authorization(QUrl const&       endpoint,
                         QByteArray const& body,
                         QString const&    secretId,
                         QString const&    secretKey,
                         qint64            timestampSeconds);

std::unique_ptr<OnlineOcrClient> makeTencentClient(TencentConfig config, std::shared_ptr<HttpTransport> transport);

/// The service's cap on the base64 image field.
inline constexpr qsizetype kTencentMaxEncodedBytes = 10'000'000;

}
