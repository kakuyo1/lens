#include "util/qt_log.h"

#include "util/log.h"

#include <QtCore/QString>
#include <QtCore/qlogging.h>

#include <memory>
#include <string>

namespace lens::log {
namespace {

spdlog::level::level_enum levelFor(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg: return spdlog::level::debug;
        case QtInfoMsg: return spdlog::level::info;
        case QtWarningMsg: return spdlog::level::warn;
        case QtCriticalMsg: return spdlog::level::err; // spdlog spells it 'err'
        case QtFatalMsg: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

/// The logger Qt forwarding writes through, captured at install and deliberately leaked.
/// spdlog's registry is a static singleton destroyed during LdrShutdownProcess, yet a Qt
/// static destructor (Qt6Quickd) can still emit a line after it; reading default_logger()
/// then is a use-after-free. The leaked shared_ptr keeps this logger and its sinks alive for
/// the whole process, so forwarding never touches the dead registry.
std::shared_ptr<spdlog::logger>* g_qtLogger = nullptr;

/// @note Runs on whatever thread Qt logged from; the sinks are the thread-safe variants.
void forwardToSpdlog(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // Qt compiles these out unless QT_MESSAGELOGCONTEXT is on, and omits the function in
    // release builds, so both are allowed to be null.
    const spdlog::source_loc location{context.file != nullptr ? context.file : "", context.line, context.function != nullptr ? context.function : ""};

    // g_qtLogger is set before the handler is installed and outlives the registry; the
    // default_logger() branch only serves a call before install, while the registry is live.
    // The message is an argument, not the format string, so braces inside it stay literal.
    spdlog::logger& logger = g_qtLogger != nullptr ? **g_qtLogger : *spdlog::default_logger();
    logger.log(location, levelFor(type), "qt: {}", message.toStdString());
}

} // namespace

void installQtMessageHandler()
{
    // init() has installed the lens logger; hold it in a leaked shared_ptr so Qt logging
    // stays valid during shutdown, after spdlog's registry static is destroyed.
    g_qtLogger = new std::shared_ptr<spdlog::logger>(spdlog::default_logger());
    qInstallMessageHandler(&forwardToSpdlog);
}

}
