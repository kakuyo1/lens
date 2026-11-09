/**
 * @file webdav_transport.cpp
 * @brief The port over the WebDAV client, for webdav_transport.h.
 */

#include "app/duties/webdav_transport.h"

#include <utility>

#include "sync/webdav_client.h"

namespace lens::app {

namespace {

/// The collection the envelope lives in, under the server address the reader entered.
constexpr auto kCollection = "Lens";

} // namespace

std::string WebdavTransport::envelopePath(const std::string& fileName)
{
    return std::string(kCollection) + "/" + fileName;
}

WebdavTransport::WebdavTransport(lens::sync::WebDavClient& dav, core::sync::SyncPartition& partition, QObject* parent)
    : TransportQueue(parent), dav_(dav), partition_(partition)
{
    connect(&dav_, &lens::sync::WebDavClient::found, this, [this](quint64 callId, bool exists) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        if (exists)
            result.gistId = envelopePath(std::string(core::sync::kFileName));
        answer(callId, std::move(result));
    });
    connect(&dav_, &lens::sync::WebDavClient::gotFile, this, [this](quint64 callId, QString content) {
        auto const* request = current(callId);
        if (request == nullptr)
            return;
        core::sync::Answer result;
        result.gistId  = request->gistId;
        result.content = content.toStdString();
        answer(callId, std::move(result));
    });
    connect(&dav_, &lens::sync::WebDavClient::putDone, this, [this](quint64 callId, QString path) {
        auto const* request = current(callId);
        if (request == nullptr)
            return;
        core::sync::Answer result;
        // A create hands the round the path it wrote, so it is stored as the cloud copy's id.
        if (request->kind == core::sync::Request::Kind::Create)
            result.gistId = path.toStdString();
        answer(callId, std::move(result));
    });
    connect(&dav_, &lens::sync::WebDavClient::deleted, this, [this](quint64 callId, QString path) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        result.gistId = path.toStdString();
        answer(callId, std::move(result));
    });
    connect(&dav_, &lens::sync::WebDavClient::failed, this, [this](quint64 callId, lens::sync::WebDavFailure why) {
        if (current(callId) == nullptr)
            return;
        using Failure = core::sync::Answer::Failure;
        core::sync::Answer result;
        switch (why) {
            case lens::sync::WebDavFailure::Unauthorised: result.failure = Failure::Unauthorised; break;
            case lens::sync::WebDavFailure::NotFound: result.failure = Failure::NotFound; break;
            case lens::sync::WebDavFailure::Proxy: result.failure = Failure::Proxy; break;
            case lens::sync::WebDavFailure::Generic: result.failure = Failure::Generic; break;
        }
        answer(callId, std::move(result));
    });
}

void WebdavTransport::dispatch(core::sync::Request const& request, std::uint64_t& callId)
{
    const QString server   = QString::fromStdString(partition_.server());
    const QString username = QString::fromStdString(partition_.username());
    const QString password = QString::fromStdString(partition_.appPassword());
    switch (request.kind) {
        case core::sync::Request::Kind::List:
            callId = dav_.propfind(server, username, password, QString::fromStdString(envelopePath(std::string(core::sync::kFileName))));
            break;
        case core::sync::Request::Kind::Read:
            callId = dav_.get(server, username, password, QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Create:
            callId = dav_.put(server, username, password, QString::fromStdString(envelopePath(request.fileName)), QString::fromStdString(request.content));
            break;
        case core::sync::Request::Kind::Update:
            callId = dav_.put(server, username, password, QString::fromStdString(request.gistId), QString::fromStdString(request.content));
            break;
        case core::sync::Request::Kind::Delete:
            callId = dav_.del(server, username, password, QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Refresh:
            // Basic auth has nothing to renew; a round that asked ends with the refusal named.
            answerCurrent(core::sync::Answer{.failure = core::sync::Answer::Failure::RefusedRefresh});
            break;
    }
}

}
