#pragma once

/**
 * @file dev_pipe.h
 * @brief The developer command pipe: a named pipe one client at a time sends command-table lines over, and gets their
 *        answers back on (docs/adr/0068).
 *
 * Compiled in only by LENS_ENABLE_DEV_SURFACE, and created only when `--dev-pipe=<name>` gives a name. It takes a client
 * of this user and this session, one at a time, and lines no longer than core::dev::LineFramer::kMaxLineBytes. Anything
 * else is refused before a command runs. The command table itself is in dev_surface.h.
 */

#include "core/dev/dev_line_framer.h"

#include <QLocalServer>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace lens::app {

/**
 * @brief The pipe's wire: connections, framing and the answers going back. It knows nothing of commands.
 */
class DevPipe : public QObject {
    Q_OBJECT

public:
    /// @param name The pipe's name, as the command line gave it. Never defaulted.
    /// @param parent QObject parent; the pipe and its client are freed with it.
    DevPipe(QString name, QObject* parent = nullptr);

    /// @brief Start listening on the name, for this user only.
    /// @return Whether the pipe is listening. False when the name is taken or cannot be created.
    bool listen();

    /// @brief Write one answer line to the connected client. With no client the line is dropped here; the log keeps it.
    void reply(QString const& line);

    /// @brief Wait, for a bounded time, until the client has the answers written so far. Called before the application
    ///        quits, so the last answer is not lost with the process.
    void drain();

signals:
    /// @brief One line from the client, without its break. Blank lines come through too; the caller skips them.
    void lineReceived(QString line);

private:
    void onNewConnection();
    void onReadyRead();
    void onClientGone();

    /// @return Whether @p client is in this process's session. The pipe handle the socket wraps answers for its client,
    ///         so a handle that is not a pipe answers false, and the client is refused.
    static bool isSameSession(QLocalSocket const& client);

    QString               name_;
    QLocalServer          server_;
    QLocalSocket*         client_{nullptr};
    core::dev::LineFramer framer_;
};

} // namespace lens::app
