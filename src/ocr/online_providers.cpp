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
         .credentials   = {QStringLiteral("apiKey"), QStringLiteral("secretKey")},
         .languages     = {{QStringLiteral("eng"), QStringLiteral("ENG")}, {QStringLiteral("auto"), QStringLiteral("CHN_ENG")}}},
        // The service with no account behind it: one key by mail, English or its own auto mode.
        {.id            = QStringLiteral("ocrspace"),
         .label         = QStringLiteral("OCR.space"),
         .group         = QStringLiteral("International"),
         .protocol      = OcrServiceProtocol::OcrSpace,
         .endpoint      = https("https://api.ocr.space/parse/image"),
         .tokenEndpoint = {},
         .docs          = QStringLiteral("https://ocr.space/ocrapi"),
         .credentials   = {QStringLiteral("apiKey")},
         .languages     = {{QStringLiteral("eng"), QStringLiteral("eng")}, {QStringLiteral("auto"), QStringLiteral("auto")}}},
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
