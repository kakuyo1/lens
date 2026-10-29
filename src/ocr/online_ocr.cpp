/** @file online_ocr.cpp
 * @brief The provider table's readers, the address rule every service shares, and the factory.
 */
#include "online_ocr.h"

#include <QUrlQuery>

#include <mutex>
#include <unordered_map>

#include "ocr_baidu.h"
#include "ocr_space.h"

namespace lens::ocr {
namespace {

struct MemoryOcrTokenStore final : OcrTokenStore {
    mutable std::mutex mutex;
    /// Mutable so a lookup can drop a token it has just found to be dead.
    mutable std::unordered_map<QString, OcrToken> tokens;

    std::optional<OcrToken> find(QString const& key) const override
    {
        std::lock_guard const lock{mutex};
        auto const            found = tokens.find(key);
        if (found == tokens.end()) return std::nullopt;
        if (found->second.expiresAt <= std::chrono::steady_clock::now()) {
            tokens.erase(found);
            return std::nullopt;
        }
        return found->second;
    }

    void put(QString const& key, OcrToken token) override
    {
        std::lock_guard const lock{mutex};
        tokens.insert_or_assign(key, std::move(token));
    }

    void erase(QString const& key) override
    {
        std::lock_guard const lock{mutex};
        tokens.erase(key);
    }

    void clear() override
    {
        std::lock_guard const lock{mutex};
        tokens.clear();
    }
};

}

std::optional<QString> validateOcrEndpoint(QUrl const& endpoint)
{
    if (not endpoint.isValid()) return QStringLiteral("invalid-url");
    if (endpoint.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) return QStringLiteral("not-https");
    if (endpoint.host().isEmpty()) return QStringLiteral("no-host");
    if (not endpoint.userInfo().isEmpty()) return QStringLiteral("userinfo");
    if (endpoint.hasFragment()) return QStringLiteral("fragment");
    auto const encoded = endpoint.toString(QUrl::FullyEncoded);
    for (auto const character : encoded) {
        if (character.unicode() < 0x20 or character.unicode() == 0x7F) return QStringLiteral("control-character");
    }
    QUrlQuery const query{endpoint};
    for (auto const& item : query.queryItems(QUrl::FullyDecoded)) {
        auto const lower = item.first.toLower();
        if (lower.contains(QLatin1String("key")) or lower.contains(QLatin1String("token")) or
            lower.contains(QLatin1String("secret")) or lower.contains(QLatin1String("password")))
            return QStringLiteral("credential-in-query");
    }
    return std::nullopt;
}

std::shared_ptr<OcrTokenStore> makeMemoryOcrTokenStore()
{
    return std::make_shared<MemoryOcrTokenStore>();
}

std::unique_ptr<OnlineOcrClient> makeOnlineOcrClient(OnlineOcrConfig const&         config,
                                                     std::shared_ptr<HttpTransport> transport,
                                                     std::shared_ptr<OcrTokenStore> tokens)
{
    if (not transport) return {};
    if (validateOcrEndpoint(config.endpoint)) return {};
    auto const wire = config.provider.languages.value(config.language);
    if (wire.isEmpty()) return {};
    for (auto const& slot : config.provider.credentials) {
        if (slot == QLatin1String("apiKey") and config.apiKey.isEmpty()) return {};
        if (slot == QLatin1String("secretKey") and config.secretKey.isEmpty()) return {};
    }

    switch (config.provider.protocol) {
        case OcrServiceProtocol::OcrSpace:
            return makeOcrSpaceClient(
                {.endpoint = config.endpoint, .apiKey = config.apiKey, .language = wire}, std::move(transport));
        case OcrServiceProtocol::Baidu:
            return makeBaiduClient({.endpoint      = config.endpoint,
                                    .tokenEndpoint = config.provider.tokenEndpoint,
                                    .apiKey        = config.apiKey,
                                    .secretKey     = config.secretKey,
                                    .language      = wire},
                                   std::move(transport),
                                   tokens ? std::move(tokens) : makeMemoryOcrTokenStore());
    }
    return {};
}

}
