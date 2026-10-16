#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @file github_auth.h
 * @brief GitHub's device flow: ask for a code, poll until the reader enters it, get a token.
 *
 * The app shows the user code and the verification address and never opens a local port: the
 * reader types the code on github.com, and this object polls the token endpoint until GitHub says
 * granted, denied, or expired. No client secret is involved, so the client id can ship in the
 * data. The only scope asked for is `gist` (docs/adr/0042).
 *
 * The parsers are free functions over the bodies, so the answers they read can be asserted without
 * a socket. Nothing here logs a device code, a user code, or a token.
 */

namespace lens::sync {

/// @brief The answer to the code request: what to show the reader, and how long to wait.
struct DeviceCode {
    QString deviceCode;      ///< Secret: this object polls with it and nothing else shows it.
    QString userCode;        ///< The eight characters the reader types on github.com.
    QString verificationUri; ///< Always https; the page the reader types the code into.
    int     expiresIn = 0;   ///< Seconds until both codes stop working.
    int     interval  = 5;   ///< Minimum seconds between two polls.
};

/// @return The code, or std::nullopt when the body is not a usable answer.
std::optional<DeviceCode> parseDeviceCode(const QByteArray& body);

/// @brief One sign-in's tokens: the bearer token every GitHub call carries, and the refresh token
///        that buys the next one once it ages out (docs/adr/0042).
struct Token {
    QString access;               ///< Secret: what api.github.com is called with. Never logged.
    QString refresh;              ///< Secret: empty when the OAuth app's tokens do not expire.
    int     expiresIn        = 0; ///< Seconds `access` lives; 0 when the app does not expire tokens.
    int     refreshExpiresIn = 0; ///< Seconds `refresh` lives unused; 0 when there is no refresh token.
};

/// @brief What one poll of the token endpoint said.
enum class PollState { Pending,
                       SlowDown,
                       Expired,
                       Denied,
                       Granted,
                       BadRefresh,
                       Failed };

/// @brief One poll's answer; the tokens are set only when the state is Granted.
struct PollResult {
    PollState state = PollState::Failed;
    Token     token;
};

/// @return The state the body names. Anything that is not a recognised answer is Failed.
PollResult parseTokenPoll(const QByteArray& body);

/// @brief Why sign-in stopped without a token.
enum class AuthFailure { Generic,
                         Expired,
                         Denied,
                         Proxy,
                         /// The stored refresh token is dead: the reader has to sign in again.
                         BadRefresh };

/**
 * @brief Runs the device flow for one sign-in at a time.
 *
 * requestCode() starts it; the object then polls on its own timer at GitHub's interval, backs off
 * when asked to slow down, and stops on its own when the code expires. A new requestCode() replaces
 * one in flight without reporting it, and cancel() stops quietly.
 *
 * An OAuth app whose user tokens expire hands back a refresh token with the access token. That one
 * is spent through refresh(), which is a single request and reports through refreshed() — the
 * polling loop above is not involved.
 */
class GithubAuth : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Build an object, optionally over a caller-supplied network manager.
     *
     * @param manager Network manager to post through, or null to make one. The default manager's
     *                proxy behaviour is the one wanted, so nothing is set explicitly.
     * @param parent  QObject parent.
     */
    explicit GithubAuth(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief Ask GitHub for a device code for @p clientId, requesting the `gist` scope.
    /// @note An empty @p clientId fails at once with Generic; the caller should not have asked.
    void requestCode(const QString& clientId);

    /**
     * @brief Spend a refresh token on a new pair of tokens.
     *
     * One request to the same endpoint, and no polling: the answer carries the new access token and a
     * new refresh token, and the one that was spent stops working (GitHub rotates both).
     *
     * @param clientId     The OAuth app the refresh token belongs to.
     * @param refreshToken The stored refresh token.
     * @note An empty @p refreshToken fails at once with Generic; the caller should not have asked.
     */
    void refresh(const QString& clientId, const QString& refreshToken);

    /// @brief Stop polling and drop the code. Nothing is reported.
    void cancel();

signals:
    /// @brief The code is issued and polling has started. Show the reader the user code and address.
    void codeReady(lens::sync::DeviceCode code);

    /// @brief The reader approved the app; the tokens are the only thing that leaves this object.
    void granted(lens::sync::Token token);

    /// @brief A refresh token bought a new pair of tokens; both must replace what was stored.
    void refreshed(lens::sync::Token token);

    /// @brief Sign-in stopped without a token, or a refresh did not happen. Carries no text.
    void failed(lens::sync::AuthFailure why);

private:
    void poll();
    void onCodeFinished(QNetworkReply* reply);
    void onPollFinished(QNetworkReply* reply);
    /// @brief Read a refresh answer: the token endpoint's own shape, through the same parser a poll uses.
    void onRefreshFinished(QNetworkReply* reply);
    /// Stops the timers, abandons the request in flight and forgets the code. Reports nothing.
    void stop();
    /// Stops, then reports @p why.
    void fail(lens::sync::AuthFailure why);

    QNetworkAccessManager* manager_;
    QString                userAgent_;
    QTimer*                pollTimer_;
    QTimer*                expiryTimer_;
    QNetworkReply*         reply_ = nullptr;
    QString                clientId_;
    DeviceCode             code_;
    int                    intervalMs_ = 5000;
};

}

Q_DECLARE_METATYPE(lens::sync::DeviceCode)
Q_DECLARE_METATYPE(lens::sync::Token)
Q_DECLARE_METATYPE(lens::sync::AuthFailure)
