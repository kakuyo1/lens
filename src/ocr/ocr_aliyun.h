/** @file ocr_aliyun.h
 * @brief Alibaba Cloud OCR (RecognizeGeneral, ocr-api 2021-07-07), signed RPC style.
 */
#pragma once

#include <QString>
#include <QUrl>

#include <memory>

#include "http_transport.h"
#include "online_ocr.h"

namespace lens::ocr {

struct AliyunConfig {
    QUrl    endpoint; ///< `https://ocr-api.cn-hangzhou.aliyuncs.com/`
    QString accessKeyId;
    QString accessKeySecret;
};

/**
 * @brief The signed request URL for one RecognizeGeneral call.
 *
 * The nonce and timestamp are parameters so the published example and a unit test can pin the
 * signature without a socket or a clock.
 */
QUrl signedRecognizeUrl(QUrl const&    endpoint,
                        QString const& accessKeyId,
                        QString const& accessKeySecret,
                        QString const& nonce,
                        QString const& timestamp);

std::unique_ptr<OnlineOcrClient> makeAliyunClient(AliyunConfig config, std::shared_ptr<HttpTransport> transport);

/// The service's cap on the binary image body.
inline constexpr qsizetype kAliyunMaxImageBytes = 10'000'000;

}
