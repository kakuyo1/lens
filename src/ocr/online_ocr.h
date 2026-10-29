/** @file online_ocr.h
 * @brief The online OCR services this build offers, and the seam each one plugs into.
 *
 * One provider is one adapter. The table in online_providers.cpp is the list a settings page and
 * the configured-check both read, so a service that has no compiled adapter cannot be offered by
 * accident: adding a service is a row, an OcrServiceProtocol value, a factory case and a file, in
 * that order. Nothing else -- not the duty, not QML -- knows a provider's name.
 */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

#include "capture/ocr_engine.h"
#include "http_transport.h"

namespace lens::ocr {

/// @brief The request shape a provider's adapter speaks. One value per implemented service.
enum class OcrServiceProtocol : std::uint8_t {
    OcrSpace,
    Baidu,
    Tencent,
    Aliyun,
    GoogleVision,
};

/// @brief One credential a service asks for: where its secret is stored, and the vendor's name.
struct OcrCredentialField {
    QString slot;  ///< `apiKey` or `secretKey`; chooses the profile key the secret is written to.
    QString label; ///< The vendor's own field name, shown as its console shows it.
};

/// @brief One selectable service: what it is called, where it listens, and what it needs.
struct OnlineOcrProvider {
    QString            id;    ///< Stored value, fixed for the life of the profile.
    QString            label; ///< Proper noun shown to the reader; not translated.
    QString            group; ///< Selection group, matching the explanation page's spelling.
    OcrServiceProtocol protocol = OcrServiceProtocol::OcrSpace;
    QUrl               endpoint;      ///< The service's default address; the reader may override it.
    QUrl               tokenEndpoint; ///< Empty for a protocol that authenticates every request directly.
    QString            docs;          ///< The vendor's own documentation for the service.
    /// The credential fields this service needs, in the order the page shows them. The slots are
    /// a fixed vocabulary -- `apiKey` and `secretKey` -- while each label is the vendor's own name
    /// for the field (`SecretId`, `AccessKey ID`, ...), the one its console shows.
    QList<OcrCredentialField> credentials;
    /// Stored language value (`eng`, `auto`) to the wire value this service wants.
    QHash<QString, QString> languages;
};

/// @return Every service this build can call, in the order a selection offers them.
QList<OnlineOcrProvider> const& onlineOcrProviders();

/// @return The service @p id names, or null when this build has no such adapter.
OnlineOcrProvider const* findOcrProvider(QString const& id);

/// @return Why @p endpoint may not be used, or nothing when it is an acceptable HTTPS OCR URL.
std::optional<QString> validateOcrEndpoint(QUrl const& endpoint);

/// @brief One consented online recognition. It is not an OcrEngine: scanning has no way to reach it.
struct OnlineOcrClient {
    virtual ~OnlineOcrClient()                                                          = default;
    virtual capture::OcrResult recognize(capture::OcrRequest const&        request,
                                         std::shared_ptr<std::atomic_bool> cancel = {}) = 0;
};

/// @brief What the factory needs: the service, its effective address, and its credentials.
struct OnlineOcrConfig {
    OnlineOcrProvider provider;
    QUrl              endpoint; ///< The reader's override, else the service's own address.
    QString           apiKey;
    QString           secretKey;
    QString           language; ///< Stored value, mapped to the wire value by the factory.
};

/// @brief A token a service handed out, and when it stops being worth using.
struct OcrToken {
    QString                               accessToken;
    std::chrono::steady_clock::time_point expiresAt;
};

/**
 * @brief The process-lifetime memo for exchanged tokens.
 *
 * A token exchange is an extra round trip, and a service like Baidu keeps its token for weeks. It
 * lives in memory only: a token is a credential, and the profile already holds the one that buys
 * it. The duty clears this whenever a credential or an address changes.
 */
struct OcrTokenStore {
    virtual ~OcrTokenStore()                                                = default;
    virtual std::optional<OcrToken> find(QString const& key) const          = 0;
    virtual void                    put(QString const& key, OcrToken token) = 0;
    virtual void                    erase(QString const& key)               = 0;
    virtual void                    clear()                                 = 0;
};

std::shared_ptr<OcrTokenStore> makeMemoryOcrTokenStore();

/**
 * @brief Build the adapter for @p config's service.
 * @return Null when the configuration is incomplete: an unknown language, an address that is not
 *         HTTPS, or a credential the service's own table requires and does not have.
 */
std::unique_ptr<OnlineOcrClient> makeOnlineOcrClient(OnlineOcrConfig const&         config,
                                                     std::shared_ptr<HttpTransport> transport,
                                                     std::shared_ptr<OcrTokenStore> tokens);

}
