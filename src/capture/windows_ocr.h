/** @file windows_ocr.h
 * @brief The deployment gate for Windows.Media.Ocr.
 */
#pragma once

#include <QString>

namespace lens::capture {

/// @brief Whether this process may use Windows OCR, and why not when it may not.
///
/// The probe in test/googletest/integration/windows_ocr_probe_test.cpp showed the API answering
/// from an unpacked process on 2026-10-28 with zh-Hans-CN installed, but Microsoft documents the
/// desktop support boundary as package identity. Lens ships unpacked, and an adapter is not part
/// of this build either, so both conditions have to be reported rather than assumed.
struct WindowsOcrSupport {
    enum class State { Usable,
                       AdapterPending,
                       DeploymentUnsupported };

    State state    = State::DeploymentUnsupported;
    bool  packaged = false;

    bool usable() const
    {
        return state == State::Usable;
    }
    /// @return The stable token a surface or a log line may name; never an error message.
    QString token() const;
};

/// @return What this process and this build can do today. No external program is started.
WindowsOcrSupport inspectWindowsOcrSupport();

}
