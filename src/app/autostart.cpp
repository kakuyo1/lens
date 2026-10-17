/**
 * @file autostart.cpp
 * @brief The Run entry that starts Lens with the reader's session.
 */

#include "app/autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "util/log.h"

namespace lens::app {

namespace {

/// @brief The command naming the executable this process runs from.
QString currentCommand()
{
    const QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QString::fromStdString(autostartCommand(path.toStdString()));
}

}

bool writeAutostart(bool on)
{
    QSettings run(kAutostartRunKey, QSettings::NativeFormat);
    if (on) {
        run.setValue(kAutostartValueName, currentCommand());
    } else {
        run.remove(kAutostartValueName);
    }
    run.sync();

    // What the return value says is whether the registry took the change, so a policy that
    // forbids the key is reported rather than quietly lost.
    const bool stored = run.status() == QSettings::NoError;
    if (!stored)
        LENS_WARN("autostart could not be turned {} (registry status {})", on ? "on" : "off", static_cast<int>(run.status()));
    else
        LENS_INFO("autostart {}", on ? "on" : "off");
    return stored;
}

void refreshAutostart()
{
    QSettings     run(kAutostartRunKey, QSettings::NativeFormat);
    const QString stored = run.value(kAutostartValueName).toString();
    if (stored.isEmpty())
        return;

    // A Run value is a quoted command line; this app writes one path and no arguments.
    QString target = stored;
    if (target.size() >= 2 and target.startsWith(QLatin1Char('"')) and target.endsWith(QLatin1Char('"')))
        target = target.mid(1, target.size() - 2);
    if (QFileInfo::exists(target))
        return;

    // The entry follows this executable only when the one it names is gone. A live entry is the
    // reader's: it keeps pointing at the copy they chose even while another build runs. A dead
    // one leaves the switch on over a path Windows skips without saying so.
    const QString command = currentCommand();
    run.setValue(kAutostartValueName, command);
    run.sync();
    if (run.status() == QSettings::NoError)
        LENS_INFO("autostart entry now points at {}", command.toStdString());
    else
        LENS_WARN("autostart entry could not follow the executable (registry status {})", static_cast<int>(run.status()));
}

bool autostartEnabled()
{
    const QSettings run(kAutostartRunKey, QSettings::NativeFormat);
    return !run.value(kAutostartValueName).toString().isEmpty();
}

}
