/** @file qt_http_transport.cpp
 * @brief Qt Network behind QtHttpTransport, with redirects and unbounded reads disabled.
 */
#include "qt_http_transport.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>

namespace lens::ocr {
namespace {

constexpr qsizetype kMaxResponseBytes = 4 * 1024 * 1024;

}

HttpResponse QtHttpTransport::send(HttpRequest const& request, std::shared_ptr<std::atomic_bool> cancel)
{
    HttpResponse result;
    if (not request.url.isValid() or request.url.scheme() != QLatin1String("https")) return result;

    QNetworkAccessManager manager;
    manager.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);

    QNetworkRequest networkRequest{request.url};
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    for (auto const& [name, value] : request.headers)
        networkRequest.setRawHeader(name, value);

    QNetworkReply* reply = nullptr;
    if (request.method == QByteArrayLiteral("POST"))
        reply = manager.post(networkRequest, request.body);
    else
        return result;
    if (reply == nullptr) return result;

    QEventLoop       loop;
    QTimer           deadline;
    QTimer           poll;
    std::atomic_bool timedOut{false};
    std::atomic_bool cancelled{false};
    std::atomic_bool tooLarge{false};

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        timedOut.store(true, std::memory_order_relaxed);
        reply->abort();
    });
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancel and cancel->load(std::memory_order_relaxed)) {
            cancelled.store(true, std::memory_order_relaxed);
            reply->abort();
            return;
        }
        if (reply->bytesAvailable() > kMaxResponseBytes) {
            tooLarge.store(true, std::memory_order_relaxed);
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        if (reply->bytesAvailable() > kMaxResponseBytes) {
            tooLarge.store(true, std::memory_order_relaxed);
            reply->abort();
        }
    });

    deadline.setSingleShot(true);
    deadline.start(std::clamp(request.timeoutMs, 100, 60000));
    poll.start(50);
    loop.exec();
    poll.stop();

    result.cancelled        = cancelled.load(std::memory_order_relaxed);
    result.timedOut         = timedOut.load(std::memory_order_relaxed);
    result.responseTooLarge = tooLarge.load(std::memory_order_relaxed);
    result.status           = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body             = reply->readAll();
    if (result.body.size() > kMaxResponseBytes) {
        result.responseTooLarge = true;
        result.body.clear();
    }
    // A 4xx/5xx is a complete HTTP answer and belongs to the adapter's mapping; only no answer at
    // all (TLS, DNS, an aborted read) is a transport failure.
    result.transportOk = not result.cancelled and not result.timedOut and not result.responseTooLarge and
                         (result.status != 0 or reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    return result;
}

}
