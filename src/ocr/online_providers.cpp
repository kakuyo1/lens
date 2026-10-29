/** @file online_providers.cpp
 * @brief The services a reader can pick, and what each one needs from them.
 *
 * Order is the order the selection shows. The table is the one place a provider id is spelled: the
 * duty refuses an id that is not here, so a row without an adapter would be a switch that offers a
 * service nothing can call -- add the adapter first, then the row.
 */
#include "online_ocr.h"

namespace lens::ocr {
namespace {

QUrl https(char const* url)
{
    return QUrl{QString::fromLatin1(url)};
}

QList<OnlineOcrProvider> const& table()
{
    static QList<OnlineOcrProvider> const providers{
        {.id            = QStringLiteral("baidu"),
         .label         = QStringLiteral("Baidu OCR"),
         .group         = QStringLiteral("Domestic"),
         .protocol      = OcrServiceProtocol::Baidu,
         .endpoint      = https("https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic"),
         .tokenEndpoint = https("https://aip.baidubce.com/oauth/2.0/token"),
         .docs          = QStringLiteral("https://ai.baidu.com/ai-doc/OCR/zk3h7xz52"),
         .credentials   = {{QStringLiteral("apiKey"), QStringLiteral("API Key")},
                           {QStringLiteral("secretKey"), QStringLiteral("Secret Key")}},
         .languages     = {{QStringLiteral("eng"), QStringLiteral("ENG")}, {QStringLiteral("auto"), QStringLiteral("CHN_ENG")}}},
        {.id            = QStringLiteral("tencent"),
         .label         = QStringLiteral("Tencent Cloud OCR"),
         .group         = QStringLiteral("Domestic"),
         .protocol      = OcrServiceProtocol::Tencent,
         .endpoint      = https("https://ocr.tencentcloudapi.com/"),
         .tokenEndpoint = {},
         .docs          = QStringLiteral("https://cloud.tencent.com/document/api/866/33526"),
         .credentials   = {{QStringLiteral("apiKey"), QStringLiteral("SecretId")},
                           {QStringLiteral("secretKey"), QStringLiteral("SecretKey")}},
         .languages     = {{QStringLiteral("eng"), QStringLiteral("en")}, {QStringLiteral("auto"), QStringLiteral("auto")}}},
        // RecognizeGeneral has no language parameter: it reads Chinese and English together, so
        // the only honest choice is the service's own default.
        {.id            = QStringLiteral("aliyun"),
         .label         = QStringLiteral("Alibaba Cloud OCR"),
         .group         = QStringLiteral("Domestic"),
         .protocol      = OcrServiceProtocol::Aliyun,
         .endpoint      = https("https://ocr-api.cn-hangzhou.aliyuncs.com/"),
         .tokenEndpoint = {},
         .docs          = QStringLiteral("https://help.aliyun.com/zh/ocr/developer-reference/api-ocr-api-2021-07-07-recognizegeneral"),
         .credentials   = {{QStringLiteral("apiKey"), QStringLiteral("AccessKey ID")},
                           {QStringLiteral("secretKey"), QStringLiteral("AccessKey Secret")}},
         .languages     = {{QStringLiteral("auto"), QString()}}},
        // The service with no account behind it: one key by mail, English or its own auto mode.
        {.id            = QStringLiteral("ocrspace"),
         .label         = QStringLiteral("OCR.space"),
         .group         = QStringLiteral("International"),
         .protocol      = OcrServiceProtocol::OcrSpace,
         .endpoint      = https("https://api.ocr.space/parse/image"),
         .tokenEndpoint = {},
         .docs          = QStringLiteral("https://ocr.space/ocrapi"),
         .credentials   = {{QStringLiteral("apiKey"), QStringLiteral("API Key")}},
         .languages     = {{QStringLiteral("eng"), QStringLiteral("eng")}, {QStringLiteral("auto"), QStringLiteral("auto")}}},
        {.id            = QStringLiteral("google"),
         .label         = QStringLiteral("Google Cloud Vision"),
         .group         = QStringLiteral("International"),
         .protocol      = OcrServiceProtocol::GoogleVision,
         .endpoint      = https("https://vision.googleapis.com/v1/images:annotate"),
         .tokenEndpoint = {},
         .docs          = QStringLiteral("https://cloud.google.com/vision/docs/ocr"),
         .credentials   = {{QStringLiteral("apiKey"), QStringLiteral("API Key")}},
         .languages     = {{QStringLiteral("eng"), QStringLiteral("en")}, {QStringLiteral("auto"), QString()}}},
    };
    return providers;
}

}

QList<OnlineOcrProvider> const& onlineOcrProviders()
{
    return table();
}

OnlineOcrProvider const* findOcrProvider(QString const& id)
{
    if (id.isEmpty()) return nullptr;
    for (auto const& provider : table())
        if (provider.id == id) return &provider;
    return nullptr;
}

}
