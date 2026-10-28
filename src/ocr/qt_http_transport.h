/** @file qt_http_transport.h
 * @brief The real transport: HTTPS, no redirect following, bounded body and time.
 */
#pragma once

#include "http_transport.h"

namespace lens::ocr {

/// @brief A blocking POST for the OCR worker thread.
///
/// Redirects are disabled rather than followed: a 3xx is returned to the adapter, which treats
/// it as an endpoint error, so an API key cannot be carried to a destination the reader did not
/// confirm. Nothing is read after the body cap is crossed.
class QtHttpTransport final : public HttpTransport {
public:
    HttpResponse send(HttpRequest const& request, std::shared_ptr<std::atomic_bool> cancel) override;
};

}
