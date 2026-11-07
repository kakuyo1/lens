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
    : QObject(parent), dav_(dav), partition_(partition)
{
    connect(&dav_, &lens::sync::WebDavClient::found, this, [this](bool exists) {
        if (!expecting(core::sync::Request::Kind::List))
            return;
        core::sync::Answer answer;
        if (exists)
            answer.gistId = envelopePath(std::string(core::sync::kFileName));
        complete(std::move(answer));
    });
    connect(&dav_, &lens::sync::WebDavClient::gotFile, this, [this](QString content) {
        if (!expecting(core::sync::Request::Kind::Read))
            return;
        core::sync::Answer answer;
        answer.gistId  = queue_.front().request.gistId;
        answer.content = content.toStdString();
        complete(std::move(answer));
    });
    connect(&dav_, &lens::sync::WebDavClient::putDone, this, [this](QString path) {
        const bool creating = expecting(core::sync::Request::Kind::Create);
        if (!creating && !expecting(core::sync::Request::Kind::Update))
            return;
        core::sync::Answer answer;
        // A create hands the round the path it wrote, so it is stored as the cloud copy's id.
        if (creating)
            answer.gistId = path.toStdString();
        complete(std::move(answer));
    });
    connect(&dav_, &lens::sync::WebDavClient::deleted, this, [this](QString path) {
        if (!expecting(core::sync::Request::Kind::Delete))
            return;
        core::sync::Answer answer;
        answer.gistId = path.toStdString();
        complete(std::move(answer));
    });
    connect(&dav_, &lens::sync::WebDavClient::failed, this, [this](lens::sync::WebDavFailure why) {
        if (!inFlight_ || queue_.empty())
            return;
        using Failure = core::sync::Answer::Failure;
        core::sync::Answer answer;
        switch (why) {
            case lens::sync::WebDavFailure::Unauthorised: answer.failure = Failure::Unauthorised; break;
            case lens::sync::WebDavFailure::NotFound: answer.failure = Failure::NotFound; break;
            case lens::sync::WebDavFailure::Proxy: answer.failure = Failure::Proxy; break;
            case lens::sync::WebDavFailure::Generic: answer.failure = Failure::Generic; break;
        }
        complete(std::move(answer));
    });
}

void WebdavTransport::send(const core::sync::Request& request, Reply reply)
{
    queue_.push_back({request, std::move(reply)});
    pump();
}

bool WebdavTransport::expecting(core::sync::Request::Kind kind) const
{
    return inFlight_ && !queue_.empty() && queue_.front().request.kind == kind;
}

void WebdavTransport::pump()
{
    if (inFlight_ || queue_.empty())
        return;
    inFlight_                          = true;
    const core::sync::Request request  = queue_.front().request;
    const QString             server   = QString::fromStdString(partition_.server());
    const QString             username = QString::fromStdString(partition_.username());
    const QString             password = QString::fromStdString(partition_.appPassword());
    switch (request.kind) {
        case core::sync::Request::Kind::List:
            dav_.propfind(server, username, password, QString::fromStdString(envelopePath(std::string(core::sync::kFileName))));
            break;
        case core::sync::Request::Kind::Read:
            dav_.get(server, username, password, QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Create:
            dav_.put(server, username, password, QString::fromStdString(envelopePath(request.fileName)), QString::fromStdString(request.content));
            break;
        case core::sync::Request::Kind::Update:
            dav_.put(server, username, password, QString::fromStdString(request.gistId), QString::fromStdString(request.content));
            break;
        case core::sync::Request::Kind::Delete:
            dav_.del(server, username, password, QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Refresh:
            // Basic auth has nothing to renew; a round that asked ends with the refusal named.
            complete(core::sync::Answer{.failure = core::sync::Answer::Failure::RefusedRefresh});
            break;
    }
}

void WebdavTransport::complete(core::sync::Answer answer)
{
    if (queue_.empty())
        return;
    Pending pending = std::move(queue_.front());
    queue_.pop_front();
    inFlight_ = false;
    pending.reply(answer);
    pump();
}

}
