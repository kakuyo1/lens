/**
 * @file online_providers_test.cpp
 * @brief The provider table's invariants and the factory's refusal rules.
 *
 * The table is what a settings page and the configured-check both read, so a row that is missing
 * an address, a credential slot or a language is a service offered and unusable.
 */
#include <gtest/gtest.h>

#include <QUrlQuery>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "ocr/http_transport.h"
#include "ocr/online_ocr.h"

namespace {

using lens::ocr::OnlineOcrConfig;
using lens::ocr::OnlineOcrProvider;

struct RecordingTransport final : lens::ocr::HttpTransport {
    std::vector<lens::ocr::HttpRequest>  requests;
    std::vector<lens::ocr::HttpResponse> responses;

    lens::ocr::HttpResponse send(lens::ocr::HttpRequest const& request, std::shared_ptr<std::atomic_bool>) override
    {
        requests.push_back(request);
        if (requests.size() > responses.size()) return {};
        return responses[requests.size() - 1];
    }
};

OnlineOcrConfig configFor(OnlineOcrProvider const& provider)
{
    return {.provider  = provider,
            .endpoint  = provider.endpoint,
            .apiKey    = QStringLiteral("api-key"),
            .secretKey = QStringLiteral("secret-key"),
            .language  = QStringLiteral("eng")};
}

TEST(OnlineProviders, EveryServiceInTheTableIsCompleteAndCallable)
{
    EXPECT_FALSE(lens::ocr::onlineOcrProviders().isEmpty());
    QStringList ids;
    for (auto const& provider : lens::ocr::onlineOcrProviders()) {
        EXPECT_FALSE(ids.contains(provider.id)) << provider.id.toStdString();
        ids << provider.id;
        EXPECT_FALSE(provider.label.isEmpty()) << provider.id.toStdString();
        EXPECT_FALSE(provider.group.isEmpty()) << provider.id.toStdString();
        EXPECT_FALSE(provider.docs.isEmpty()) << provider.id.toStdString();
        EXPECT_FALSE(lens::ocr::validateOcrEndpoint(provider.endpoint)) << provider.id.toStdString();
        if (not provider.tokenEndpoint.isEmpty())
            EXPECT_FALSE(lens::ocr::validateOcrEndpoint(provider.tokenEndpoint)) << provider.id.toStdString();
        EXPECT_FALSE(provider.credentials.isEmpty()) << provider.id.toStdString();
        for (auto const& slot : provider.credentials)
            EXPECT_TRUE(slot == QLatin1String("apiKey") or slot == QLatin1String("secretKey"))
                << "unknown credential slot " << slot.toStdString();
        EXPECT_FALSE(provider.languages.value(QStringLiteral("eng")).isEmpty()) << provider.id.toStdString();
        EXPECT_FALSE(provider.languages.value(QStringLiteral("auto")).isEmpty()) << provider.id.toStdString();
        EXPECT_TRUE(lens::ocr::makeOnlineOcrClient(configFor(provider),
                                                   std::make_shared<RecordingTransport>(),
                                                   lens::ocr::makeMemoryOcrTokenStore()))
            << "no adapter answers for " << provider.id.toStdString();
    }
}

TEST(OnlineProviders, AnEmptyOrUnknownIdNamesNoService)
{
    EXPECT_EQ(lens::ocr::findOcrProvider(QString{}), nullptr);
    EXPECT_EQ(lens::ocr::findOcrProvider(QStringLiteral("nope")), nullptr);
    auto const* const provider = lens::ocr::findOcrProvider(QStringLiteral("baidu"));
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->label, QStringLiteral("Baidu OCR"));
    EXPECT_EQ(lens::ocr::findOcrProvider(provider->id), provider);
}

TEST(OnlineProviders, TheFactoryRefusesWhatTheServiceTableRequires)
{
    auto              transport = std::make_shared<RecordingTransport>();
    auto              tokens    = lens::ocr::makeMemoryOcrTokenStore();
    auto const* const baidu     = lens::ocr::findOcrProvider(QStringLiteral("baidu"));
    ASSERT_NE(baidu, nullptr);

    EXPECT_TRUE(lens::ocr::makeOnlineOcrClient(configFor(*baidu), transport, tokens));
    auto missingSecret = configFor(*baidu);
    missingSecret.secretKey.clear();
    EXPECT_FALSE(lens::ocr::makeOnlineOcrClient(missingSecret, transport, tokens));
    auto unknownLanguage     = configFor(*baidu);
    unknownLanguage.language = QStringLiteral("zh-Hans");
    EXPECT_FALSE(lens::ocr::makeOnlineOcrClient(unknownLanguage, transport, tokens));
    auto plainAddress     = configFor(*baidu);
    plainAddress.endpoint = QUrl{QStringLiteral("http://aip.baidubce.com/rest/2.0/ocr/v1/general_basic")};
    EXPECT_FALSE(lens::ocr::makeOnlineOcrClient(plainAddress, transport, tokens));

    auto const* const ocrspace = lens::ocr::findOcrProvider(QStringLiteral("ocrspace"));
    ASSERT_NE(ocrspace, nullptr);
    auto noKey = configFor(*ocrspace);
    noKey.apiKey.clear();
    EXPECT_FALSE(lens::ocr::makeOnlineOcrClient(noKey, transport, tokens));
    EXPECT_TRUE(lens::ocr::makeOnlineOcrClient(configFor(*ocrspace), transport, tokens));
}

TEST(OnlineProviders, TheStoredLanguageBecomesTheServicesOwnValue)
{
    auto const* const baidu = lens::ocr::findOcrProvider(QStringLiteral("baidu"));
    ASSERT_NE(baidu, nullptr);
    auto transport       = std::make_shared<RecordingTransport>();
    transport->responses = {{.status      = 200,
                             .body        = QByteArray{R"({"access_token":"t","expires_in":3600})"},
                             .transportOk = true},
                            {.status = 200, .body = QByteArray{R"({"words_result":[],"words_result_num":0})"}, .transportOk = true}};
    auto config          = configFor(*baidu);
    config.language      = QStringLiteral("auto");
    auto client          = lens::ocr::makeOnlineOcrClient(config, transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    lens::capture::OcrRequest request{.requestId = 1,
                                      .source    = lens::capture::OcrSource::Screenshot,
                                      .image     = QImage{200, 100, QImage::Format_RGB32},
                                      .deadline  = std::chrono::seconds{30}};
    client->recognize(request);
    ASSERT_GE(transport->requests.size(), 2u);
    EXPECT_TRUE(transport->requests[1].body.contains("language_type=CHN_ENG"));
}

TEST(OnlineTokens, ADeadOrClearedTokenIsNotHandedOut)
{
    auto store = lens::ocr::makeMemoryOcrTokenStore();
    store->put(QStringLiteral("k"),
               {.accessToken = QStringLiteral("t"), .expiresAt = std::chrono::steady_clock::now() - std::chrono::seconds{1}});
    EXPECT_FALSE(store->find(QStringLiteral("k")));
    store->put(QStringLiteral("k"),
               {.accessToken = QStringLiteral("t"), .expiresAt = std::chrono::steady_clock::now() + std::chrono::hours{1}});
    ASSERT_TRUE(store->find(QStringLiteral("k")));
    EXPECT_EQ(store->find(QStringLiteral("k"))->accessToken, QStringLiteral("t"));
    store->erase(QStringLiteral("k"));
    EXPECT_FALSE(store->find(QStringLiteral("k")));
    store->put(QStringLiteral("k"),
               {.accessToken = QStringLiteral("t"), .expiresAt = std::chrono::steady_clock::now() + std::chrono::hours{1}});
    store->clear();
    EXPECT_FALSE(store->find(QStringLiteral("k")));
}

}
