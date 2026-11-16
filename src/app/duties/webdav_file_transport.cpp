/**
 * @file webdav_file_transport.cpp
 * @brief The port over the WebDAV client for the event channel, for webdav_file_transport.h.
 */

#include "webdav_file_transport.h"

#include <QString>

#include <utility>

#include "core/domain/sync_partition.h"
#include "sync/webdav_client.h"

namespace lens::app {

QString WebdavFileTransport::collection()
{
    return QString::fromUtf8(core::sync::kWebdavCollection.data(), static_cast<qsizetype>(core::sync::kWebdavCollection.size()));
}

QString WebdavFileTransport::eventPath(const std::string& name)
{
    return collection() + QLatin1Char('/') + QString::fromStdString(name);
}

WebdavFileTransport::WebdavFileTransport(lens::sync::WebDavClient& dav, core::sync::SyncPartition& partition, QObject* parent)
    : FileTransportQueue(parent), dav_(dav), partition_(partition)
{
    connect(&dav_, &lens::sync::WebDavClient::listed, this, [this](quint64 callId, QStringList const& names) {
        if (current(callId) == nullptr)
            return;
        core::sync::FileAnswer result;
        for (QString const& name : names)
            result.names.push_back(name.toStdString());
        answer(callId, result);
    });
    connect(&dav_, &lens::sync::WebDavClient::gotFile, this, [this](quint64 callId, QString const& content) {
        if (current(callId) == nullptr)
            return;
        core::sync::FileAnswer result;
        result.content = content.toStdString();
        answer(callId, result);
    });
    connect(&dav_, &lens::sync::WebDavClient::putDone, this, [this](quint64 callId, QString const&) {
        if (current(callId) == nullptr)
            return;
        answer(callId, core::sync::FileAnswer{});
    });
    connect(&dav_, &lens::sync::WebDavClient::deleted, this, [this](quint64 callId, QString const&) {
        if (current(callId) == nullptr)
            return;
        answer(callId, core::sync::FileAnswer{});
    });
    connect(&dav_, &lens::sync::WebDavClient::failed, this, [this](quint64 callId, lens::sync::WebDavFailure why) {
        using Failure = core::sync::FileAnswer::Failure;
        if (current(callId) == nullptr)
            return;
        core::sync::FileAnswer result;
        switch (why) {
            case lens::sync::WebDavFailure::Unauthorised: result.failure = Failure::Unauthorised; break;
            case lens::sync::WebDavFailure::NotFound: result.failure = Failure::NotFound; break;
            case lens::sync::WebDavFailure::Proxy: result.failure = Failure::Proxy; break;
            case lens::sync::WebDavFailure::Generic: result.failure = Failure::Generic; break;
        }
        answer(callId, result);
    });
}

void WebdavFileTransport::dispatch(const core::sync::FileRequest& request, std::uint64_t& callId)
{
    using Failure = core::sync::FileAnswer::Failure;

    const QString server   = QString::fromStdString(partition_.server());
    const QString username = QString::fromStdString(partition_.username());
    const QString password = QString::fromStdString(partition_.appPassword());
    if (server.isEmpty() or username.isEmpty() or password.isEmpty()) {
        answerCurrent(core::sync::FileAnswer{.failure = Failure::Unconfigured});
        return;
    }

    switch (request.kind) {
        case core::sync::FileRequest::Kind::List:
            callId = dav_.list(server, username, password, collection());
            break;
        case core::sync::FileRequest::Kind::Put:
            callId = dav_.put(server, username, password, eventPath(request.name), QString::fromStdString(request.content));
            break;
        case core::sync::FileRequest::Kind::Delete:
            callId = dav_.del(server, username, password, eventPath(request.name));
            break;
        case core::sync::FileRequest::Kind::Get:
            callId = dav_.get(server, username, password, eventPath(request.name));
            break;
    }
}

}
