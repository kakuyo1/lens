/**
 * @file gist_file_transport.cpp
 * @brief The port over the gist client for the event channel, for gist_file_transport.h.
 */

#include "gist_file_transport.h"

#include <QString>

#include <utility>

#include "core/domain/sync_partition.h"
#include "sync/gist_client.h"
#include "util/log.h"

namespace lens::app {

GistFileTransport::GistFileTransport(lens::sync::GistClient& gist, core::sync::SyncPartition& partition, QObject* parent)
    : FileTransportQueue(parent), gist_(gist), partition_(partition)
{
    connect(&gist_, &lens::sync::GistClient::listedFiles, this, [this](quint64 callId, lens::sync::Gist const& gist) {
        if (current(callId) == nullptr)
            return;
        rawUrls_.clear();
        core::sync::FileAnswer result;
        for (auto it = gist.files.cbegin(); it != gist.files.cend(); ++it) {
            result.names.push_back(it.key().toStdString());
            rawUrls_.insert(it.key(), it.value().rawUrl);
        }
        answer(callId, result);
    });
    connect(&gist_, &lens::sync::GistClient::updated, this, [this](quint64 callId, lens::sync::Gist const&) {
        if (current(callId) == nullptr)
            return;
        answer(callId, core::sync::FileAnswer{});
    });
    connect(&gist_, &lens::sync::GistClient::gotRawFile, this, [this](quint64 callId, QString const& content) {
        if (current(callId) == nullptr)
            return;
        core::sync::FileAnswer result;
        result.content = content.toStdString();
        answer(callId, result);
    });
    connect(&gist_, &lens::sync::GistClient::failed, this, [this](quint64 callId, lens::sync::GistFailure why) {
        using Failure = core::sync::FileAnswer::Failure;
        if (current(callId) == nullptr)
            return;
        core::sync::FileAnswer result;
        switch (why) {
            case lens::sync::GistFailure::Unauthorised: result.failure = Failure::Unauthorised; break;
            case lens::sync::GistFailure::NotFound: result.failure = Failure::NotFound; break;
            case lens::sync::GistFailure::Proxy: result.failure = Failure::Proxy; break;
            case lens::sync::GistFailure::Generic: result.failure = Failure::Generic; break;
        }
        answer(callId, result);
    });
}

void GistFileTransport::dispatch(const core::sync::FileRequest& request, std::uint64_t& callId)
{
    using Failure = core::sync::FileAnswer::Failure;

    const QString token  = QString::fromStdString(partition_.token());
    const QString gistId = QString::fromStdString(partition_.gistId());
    if (token.isEmpty() or gistId.isEmpty()) {
        answerCurrent(core::sync::FileAnswer{.failure = Failure::Unconfigured});
        return;
    }

    const QString name = QString::fromStdString(request.name);
    switch (request.kind) {
        case core::sync::FileRequest::Kind::List:
            callId = gist_.listGistFiles(token, gistId);
            break;
        case core::sync::FileRequest::Kind::Put:
            callId = gist_.updateGistFiles(token, gistId, {lens::sync::GistFileChange{.name = name, .content = QString::fromStdString(request.content)}});
            break;
        case core::sync::FileRequest::Kind::Delete:
            callId = gist_.updateGistFiles(token, gistId, {lens::sync::GistFileChange{.name = name, .content = std::nullopt}});
            break;
        case core::sync::FileRequest::Kind::Get: {
            const auto found = rawUrls_.constFind(name);
            if (found == rawUrls_.constEnd() or found->isEmpty()) {
                LENS_WARN("gist file transport: no raw address for the month file '{}'; a listing must come first", request.name);
                answerCurrent(core::sync::FileAnswer{.failure = Failure::Generic});
                return;
            }
            callId = gist_.getRawFile(token, *found);
            break;
        }
    }
}

}
