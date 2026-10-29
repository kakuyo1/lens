/** @file ocr_baidu.h
 * @brief Baidu's general text recognition: an OAuth token, one form post, and line boxes.
 */
#pragma once

#include <QString>
#include <QUrl>

#include <memory>

#include "capture/ocr_engine.h"
#include "http_transport.h"
#include "online_ocr.h"

namespace lens::ocr {

struct BaiduConfig {
    QUrl    endpoint;      ///< The recognition address, without the token.
    QUrl    tokenEndpoint; ///< Where the client exchanges its key pair for an access token.
    QString apiKey;
    QString secretKey;
    QString language; ///< The service's own value: `ENG` or `CHN_ENG`.
};

/// @brief Build the adapter over @p transport; the token store survives between captures.
std::unique_ptr<OnlineOcrClient> makeBaiduClient(BaiduConfig                    config,
                                                 std::shared_ptr<HttpTransport> transport,
                                                 std::shared_ptr<OcrTokenStore> tokens);

/// The service's own cap on the base64 body it accepts, before its 4 MB limit.
inline constexpr qsizetype kBaiduMaxEncodedBytes = 4'000'000;

}
