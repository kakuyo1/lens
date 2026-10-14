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

/// @brief What one poll of the token endpoint said.
enum class PollState { Pending,
                       SlowDown,
                       Expired,
                       Denied,
                       Granted,
                       Failed };

/// @brief One poll's answer; the token is set only when the state is Granted.
struct PollResult {
    PollState state = PollState::Failed;
    QString   accessToken;
};

/// @return The state the body names. Anything that is not a recognised answer is Failed.
PollResult parseTokenPoll(const QByteArray& body);

/// @brief Why sign-in stopped without a token.
enum class AuthFailure { Generic,
                         Expired,
                         Denied,
                         Proxy };

/**
 * @brief Runs the device flow for one sign-in at a time.
 *
 * requestCode() starts it; the object then polls on its own timer at GitHub's interval, backs off
 * when asked to slow down, and stops on its own when the code expires. A new requestCode() replaces
 * one in flight without reporting it, and cancel() stops quietly.
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

    /// @brief Stop polling and drop the code. Nothing is reported.
    void cancel();

signals:
    /// @brief The code is issued and polling has started. Show the reader the user code and address.
    void codeReady(lens::sync::DeviceCode code);

    /// @brief The reader approved the app; the token is the only thing that leaves this object.
    void granted(QString accessToken);

    /// @brief Sign-in stopped without a token. Carries no text: the reason is the kind.
    void failed(lens::sync::AuthFailure why);

private:
    void poll();
    void onCodeFinished(QNetworkReply* reply);
    void onPollFinished(QNetworkReply* reply);
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
Q_DECLARE_METATYPE(lens::sync::AuthFailure)
