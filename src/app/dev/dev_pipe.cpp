/**
 * @file dev_pipe.cpp
 * @brief The developer command pipe's wire, for dev_pipe.h (docs/adr/0068).
 */

#include "dev_pipe.h"

#include "util/log.h"

#include <QLocalSocket>

#include <string_view>
#include <utility>

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among them), and including it
// first would let them loose on the standard library. See mouse_selection_hook.cpp.
#include <windows.h>

namespace lens::app {

namespace {

/// The most a client may have waiting in the socket. Past it the socket stops reading, so a client that sends faster
/// than the lines are answered waits instead of growing a buffer without bound.
constexpr qint64 kReadBufferBytes = 64 * 1024;
/// How long quit waits for the last answer to reach the client.
constexpr int kDrainMs = 1000;

} // namespace

DevPipe::DevPipe(QString name, QObject* parent)
    : QObject(parent), name_(std::move(name))
{
}

bool DevPipe::listen()
{
    // Only this user's account may connect (QLocalServer::UserAccessOption); isSameSession() is the second check.
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (not server_.listen(name_)) {
        LENS_ERROR("dev pipe: cannot listen on '{}': {}", name_.toStdString(), server_.errorString().toStdString());
        return false;
    }
    connect(&server_, &QLocalServer::newConnection, this, &DevPipe::onNewConnection);
    LENS_INFO("dev pipe: listening on '{}'", name_.toStdString());
    return true;
}

void DevPipe::reply(QString const& line)
{
    if (client_ == nullptr)
        return;
    client_->write((line + QLatin1Char('\n')).toUtf8());
    client_->flush();
}

void DevPipe::drain()
{
    if (client_ != nullptr)
        client_->waitForBytesWritten(kDrainMs);
}

void DevPipe::onNewConnection()
{
    while (server_.hasPendingConnections()) {
        auto* const socket = server_.nextPendingConnection();
        socket->setParent(this);
        // Each refused client is closed at once; the one accepted client keeps the pipe until it leaves.
        if (client_ != nullptr) {
            LENS_WARN("dev pipe: a second client was refused; one client at a time");
            socket->abort();
            socket->deleteLater();
            continue;
        }
        if (not isSameSession(*socket)) {
            LENS_WARN("dev pipe: a client from another session was refused");
            socket->abort();
            socket->deleteLater();
            continue;
        }
        client_ = socket;
        framer_ = core::dev::LineFramer{};
        client_->setReadBufferSize(kReadBufferBytes);
        connect(client_, &QLocalSocket::readyRead, this, &DevPipe::onReadyRead);
        connect(client_, &QLocalSocket::disconnected, this, &DevPipe::onClientGone);
        LENS_INFO("dev pipe: client connected");
    }
}

void DevPipe::onReadyRead()
{
    auto const bytes  = client_->readAll();
    auto const framed = framer_.feed(std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())});
    for (auto const& line : framed.lines)
        emit lineReceived(QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size())));
    if (framed.overflow) {
        LENS_WARN("dev pipe: a line passed {} bytes; the client is cut off", core::dev::LineFramer::kMaxLineBytes);
        client_->abort();
    }
}

void DevPipe::onClientGone()
{
    LENS_INFO("dev pipe: client disconnected");
    client_->deleteLater();
    client_ = nullptr;
}

bool DevPipe::isSameSession(QLocalSocket const& client)
{
    // Qt 6.9 gives a local socket no call that names its peer process, so the handle the socket wraps is asked directly.
    auto const pipe = reinterpret_cast<HANDLE>(client.socketDescriptor());
    DWORD      clientSession{};
    DWORD      ourSession{};
    if (GetNamedPipeClientSessionId(pipe, &clientSession) == FALSE)
        return false;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &ourSession) == FALSE)
        return false;
    return clientSession == ourSession;
}

} // namespace lens::app
