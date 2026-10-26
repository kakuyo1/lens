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
    : QObject(parent), gist_(gist), auth_(auth), clientId_(std::move(clientId))
{
    connect(&gist_, &lens::sync::GistClient::listed, this, [this](QByteArray body) {
        if (!expecting(core::sync::Request::Kind::List))
            return;
        core::sync::Answer answer;
        const auto         id = lens::sync::findGistId(body, QString::fromStdString(queue_.front().request.description));
        if (id)
            answer.gistId = id->toStdString();
        complete(std::move(answer));
    });
    connect(&gist_, &lens::sync::GistClient::gotGist, this, [this](lens::sync::Gist gist) {
        if (!expecting(core::sync::Request::Kind::Read))
            return;
        core::sync::Answer answer;
        answer.gistId  = gist.id.toStdString();
        answer.content = gist.files.value(QString::fromStdString(queue_.front().request.fileName)).content.toStdString();
        complete(std::move(answer));
    });
    connect(&gist_, &lens::sync::GistClient::created, this, [this](lens::sync::Gist gist) {
        if (!expecting(core::sync::Request::Kind::Create))
            return;
        core::sync::Answer answer;
        answer.gistId = gist.id.toStdString();
        complete(std::move(answer));
    });
    connect(&gist_, &lens::sync::GistClient::updated, this, [this](lens::sync::Gist) {
        if (!expecting(core::sync::Request::Kind::Update))
            return;
        complete(core::sync::Answer{});
    });
    connect(&gist_, &lens::sync::GistClient::deleted, this, [this](QString id) {
        if (!expecting(core::sync::Request::Kind::Delete))
            return;
        core::sync::Answer answer;
        answer.gistId = id.toStdString();
        complete(std::move(answer));
    });
    connect(&gist_, &lens::sync::GistClient::failed, this, [this](lens::sync::GistFailure why) {
        // A refresh goes through the auth client, whose own failure signal says nothing about a
        // gist call; and a sign-in's getName failure arrives while no round is asking.
        if (!inFlight_ || queue_.empty() || queue_.front().request.kind == core::sync::Request::Kind::Refresh)
            return;
        core::sync::Answer answer;
        answer.failure = failureOf(why);
        complete(std::move(answer));
    });
    connect(&auth_, &lens::sync::GithubAuth::refreshed, this, [this](lens::sync::Token token) {
        if (!expecting(core::sync::Request::Kind::Refresh))
            return;
        core::sync::Answer answer;
        answer.access  = token.access.toStdString();
        answer.refresh = token.refresh.toStdString();
        complete(std::move(answer));
    });
    connect(&auth_, &lens::sync::GithubAuth::failed, this, [this](lens::sync::AuthFailure why) {
        // A sign-in poll has its own handler in the duty; only a refresh this port asked for is
        // its to report.
        if (!expecting(core::sync::Request::Kind::Refresh))
            return;
        core::sync::Answer answer;
        answer.failure = failureOf(why);
        complete(std::move(answer));
    });
}

void SyncTransport::send(const core::sync::Request& request, Reply reply)
{
    queue_.push_back({request, std::move(reply)});
    pump();
}

bool SyncTransport::expecting(core::sync::Request::Kind kind) const
{
    return inFlight_ && !queue_.empty() && queue_.front().request.kind == kind;
}

void SyncTransport::pump()
{
    if (inFlight_ || queue_.empty())
        return;
    inFlight_                         = true;
    const core::sync::Request request = queue_.front().request;
    switch (request.kind) {
        case core::sync::Request::Kind::List:
            gist_.listGists(QString::fromStdString(request.token));
            break;
        case core::sync::Request::Kind::Read:
            gist_.getGist(QString::fromStdString(request.token), QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Create: {
            const QMap<QString, QString> files{{QString::fromStdString(request.fileName), QString::fromStdString(request.content)}};
            gist_.createGist(QString::fromStdString(request.token), QString::fromStdString(request.description), files);
            break;
        }
        case core::sync::Request::Kind::Update: {
            const QMap<QString, QString> files{{QString::fromStdString(request.fileName), QString::fromStdString(request.content)}};
            gist_.updateGist(QString::fromStdString(request.token), QString::fromStdString(request.gistId), files);
            break;
        }
        case core::sync::Request::Kind::Delete:
            gist_.deleteGist(QString::fromStdString(request.token), QString::fromStdString(request.gistId));
            break;
        case core::sync::Request::Kind::Refresh:
            auth_.refresh(clientId_, QString::fromStdString(request.token));
            break;
    }
}

void SyncTransport::complete(core::sync::Answer answer)
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
