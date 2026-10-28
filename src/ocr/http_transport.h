/** @file http_transport.h
 * @brief The one HTTP operation the online OCR adapter needs, injectable for offline tests.
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>

#include <atomic>
#include <memory>

namespace lens::ocr {

struct HttpRequest {
    QUrl                                 url;
    QByteArray                           method{"POST"};
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray                           body;
    int                                  timeoutMs = 30000;
};

struct HttpResponse {
    int        status = 0;
    QByteArray body;
    bool       transportOk      = false;
    bool       timedOut         = false;
    bool       cancelled        = false;
    bool       responseTooLarge = false;
};

struct HttpTransport {
    virtual ~HttpTransport()                                                                        = default;
    virtual HttpResponse send(HttpRequest const& request, std::shared_ptr<std::atomic_bool> cancel) = 0;
};

}
