/**
 * @file crash_notice_duty.cpp
 * @brief The crash marker's one trip through a launch: read, prune, announce, remove.
 */

#include "crash_notice_duty.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>

#include <fstream>
#include <sstream>

#include "app/notice.h"
#include "app/duties/explanation_duty.h"
#include "util/log.h"

namespace lens::app {

CrashNoticeDuty::CrashNoticeDuty(ExplanationDuty& notices, QObject* parent)
    : QObject(parent),
      notices_(notices)
{
}

void CrashNoticeDuty::reportStartupCrash(std::filesystem::path const& logDir)
{
    dumpDir_                           = util::crashDumpDir(logDir);
    const std::filesystem::path marker = dumpDir_ / util::kCrashMarkerName;
    std::error_code             ec;
    if (!std::filesystem::exists(marker, ec))
        return;

    // The marker carries the newest crash's summary verbatim; it is read before the notice's
    // actions can need it and before it is removed. The read handle closes before that removal:
    // an open file cannot be removed on Windows, and the marker would survive its own notice.
    std::string markerText;
    {
        std::ifstream      markerFile(marker);
        std::ostringstream text;
        text << markerFile.rdbuf();
        markerText = text.str();
    }
    summary_ = util::parseCrashSummary(markerText);
    util::pruneCrashDumps(dumpDir_);

    // The crash path could not log -- its writer runs on a heap that may be the problem -- so
    // this line is the one record that the chain from crash to artifacts completed.
    LENS_INFO("the previous run crashed ({}); the notice offers the dump folder and an issue prefill",
              summary_.code.empty() ? "unknown" : summary_.code);
    // The strings keep the facade's context, where they were spelled before the notice got its own
    // duty; lupdate reads literal arguments only, so the context is written out at each call.
    notices_.showNotice(QCoreApplication::translate("lens::app::AppController", "The previous run crashed"),
                        QCoreApplication::translate("lens::app::AppController", "A crash dump was saved in the log's dumps folder. Nothing in it leaves this machine; attach it when you report the crash."),
                        QString::fromLatin1(kNoticeCrash),
                        QCoreApplication::translate("lens::app::AppController", "Open dump folder"),
                        {},
                        QCoreApplication::translate("lens::app::AppController", "Report issue"));
    std::filesystem::remove(marker, ec);
}

void CrashNoticeDuty::openDumpFolder() const
{
    // The folder, not a picked file: the reader attaches what they choose.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dumpDir_.wstring())));
}

void CrashNoticeDuty::reportIssue() const
{
    // The URL is the pure builder's (docs/adr/0175), shared with the reporter process so the
    // template cannot drift between them; the dump leaves the machine only because the reader
    // attaches it by hand (SECURITY.md: nothing Lens holds is uploaded on its own).
    QDesktopServices::openUrl(QUrl(QString::fromStdString(util::crashIssueUrl(summary_))));
}

}
