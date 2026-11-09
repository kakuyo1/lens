/**
 * @file sync_transport.cpp
 * @brief The port over the gist and auth clients, for sync_transport.h.
 */

#include "sync_transport.h"

#include <QByteArray>
#include <QMap>

#include <utility>

#include "sync/gist_client.h"
#include "sync/github_auth.h"

namespace lens::app {

namespace {

core::sync::Answer::Failure failureOf(lens::sync::GistFailure why)
{
    using Failure = core::sync::Answer::Failure;
    switch (why) {
        case lens::sync::GistFailure::Unauthorised: return Failure::Unauthorised;
        case lens::sync::GistFailure::NotFound: return Failure::NotFound;
        case lens::sync::GistFailure::Proxy: return Failure::Proxy;
        case lens::sync::GistFailure::Generic: break;
    }
    return Failure::Generic;
}

core::sync::Answer::Failure failureOf(lens::sync::AuthFailure why)
{
    using Failure = core::sync::Answer::Failure;
    switch (why) {
        case lens::sync::AuthFailure::BadRefresh: return Failure::RefusedRefresh;
        case lens::sync::AuthFailure::Proxy: return Failure::Proxy;
        case lens::sync::AuthFailure::Expired: break;
        case lens::sync::AuthFailure::Denied: break;
        case lens::sync::AuthFailure::Generic: break;
    }
    return Failure::Generic;
}

}

SyncTransport::SyncTransport(lens::sync::GistClient& gist, lens::sync::GithubAuth& auth, QString clientId, QObject* parent)
    : TransportQueue(parent), gist_(gist), auth_(auth), clientId_(std::move(clientId))
{
    connect(&gist_, &lens::sync::GistClient::listed, this, [this](quint64 callId, QByteArray body) {
        auto const* request = current(callId);
        if (request == nullptr)
            return;
        core::sync::Answer result;
        const auto         found = lens::sync::findGistId(body, QString::fromStdString(request->description));
        if (found)
            result.gistId = found->toStdString();
        answer(callId, std::move(result));
    });
    connect(&gist_, &lens::sync::GistClient::gotGist, this, [this](quint64 callId, lens::sync::Gist gist) {
        auto const* request = current(callId);
        if (request == nullptr)
            return;
        core::sync::Answer result;
        result.gistId  = gist.id.toStdString();
        result.content = gist.files.value(QString::fromStdString(request->fileName)).content.toStdString();
        answer(callId, std::move(result));
    });
    connect(&gist_, &lens::sync::GistClient::created, this, [this](quint64 callId, lens::sync::Gist gist) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        result.gistId = gist.id.toStdString();
        answer(callId, std::move(result));
    });
    connect(&gist_, &lens::sync::GistClient::updated, this, [this](quint64 callId, lens::sync::Gist) {
        if (current(callId) == nullptr)
            return;
        answer(callId, core::sync::Answer{});
    });
    connect(&gist_, &lens::sync::GistClient::deleted, this, [this](quint64 callId, QString id) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        result.gistId = id.toStdString();
        answer(callId, std::move(result));
    });
    connect(&gist_, &lens::sync::GistClient::failed, this, [this](quint64 callId, lens::sync::GistFailure why) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        result.failure = failureOf(why);
        answer(callId, std::move(result));
    });
    connect(&auth_, &lens::sync::GithubAuth::refreshed, this, [this](quint64 callId, lens::sync::Token token) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        result.access  = token.access.toStdString();
        result.refresh = token.refresh.toStdString();
        answer(callId, std::move(result));
    });
    connect(&auth_, &lens::sync::GithubAuth::refreshFailed, this, [this](quint64 callId, lens::sync::AuthFailure why) {
        if (current(callId) == nullptr)
            return;
        core::sync::Answer result;
        result.failure = failureOf(why);
        answer(callId, std::move(result));
    });
}

void SyncTransport::dispatch(core::sync::Request const& request, std::uint64_t& callId)
{
    switch (request.kind) {
        case core::sync::Request::Kind::List:
            callId = gist_.listGists(QString::fromStdString(request.token));
            break;
        case core::sync::Request::Kind::Read:
            callId = gist_.getGist(QString::fromStdString(request.token), QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Create: {
            const QMap<QString, QString> files{{QString::fromStdString(request.fileName), QString::fromStdString(request.content)}};
            callId = gist_.createGist(QString::fromStdString(request.token), QString::fromStdString(request.description), files);
            break;
        }
        case core::sync::Request::Kind::Update: {
            const QMap<QString, QString> files{{QString::fromStdString(request.fileName), QString::fromStdString(request.content)}};
            callId = gist_.updateGist(QString::fromStdString(request.token), QString::fromStdString(request.gistId), files);
            break;
        }
        case core::sync::Request::Kind::Delete:
            callId = gist_.deleteGist(QString::fromStdString(request.token), QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Refresh:
            callId = auth_.refresh(clientId_, QString::fromStdString(request.token));
            break;
    }
}

}
