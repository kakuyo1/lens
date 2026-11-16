#pragma once

#include <QObject>

#include <filesystem>

#include "util/crash.h"

/**
 * @file crash_notice_duty.h
 * @brief The crash notice: the launch after a crash turns the marker into one card, and the two
 *        actions on it touch the dump folder and the issue prefill (docs/adr/0174, 0175, 0184).
 *
 * The facade keeps the QML spellings and forwards here; what this class owns is the sequence that
 * has rules in it: read the marker before its removal (an open file cannot be removed on Windows),
 * prune the dumps before the folder is offered, show the notice, and only then remove the marker,
 * so one crash is announced once. The facts in the issue URL are the pure builder's (src/util/crash.h),
 * shared with the reporter process so the template cannot drift between them.
 */

namespace lens::app {

class ExplanationDuty;

class CrashNoticeDuty final : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Bind the duty to the notice channel the card is shown through.
     * @param notices The explanation duty's notice slot, owned by the caller and outliving this object.
     */
    explicit CrashNoticeDuty(ExplanationDuty& notices, QObject* parent = nullptr);

    /**
     * @brief The launch after a crash: prune the dumps, turn the marker the crash handler left
     *        into a notice, and remove the marker so one crash is announced once.
     * @param logDir The log directory; the dumps live under its `dumps` subdirectory.
     */
    void reportStartupCrash(std::filesystem::path const& logDir);

    /// @brief Open the dump folder where the last crash's dump and summary sit (docs/adr/0174).
    void openDumpFolder() const;
    /**
     * @brief Open a new GitHub issue prefilled with the last crash's facts.
     * @note The dump travels only because the reader attaches it by hand (SECURITY.md).
     */
    void reportIssue() const;

private:
    ExplanationDuty& notices_;

    /// Where the crash dumps live, recorded when the startup report ran; the two actions act on
    /// it. Empty until a launch with a crash marker has happened.
    std::filesystem::path dumpDir_;
    /// The summary the crash marker carried, quoted by the issue prefill.
    util::CrashSummary summary_;
};

}
