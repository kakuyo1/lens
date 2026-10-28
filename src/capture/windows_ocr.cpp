/** @file windows_ocr.cpp
 * @brief Package-identity check behind windows_ocr.h.
 */
#include "windows_ocr.h"

#include <Windows.h>
#include <appmodel.h>

namespace lens::capture {

QString WindowsOcrSupport::token() const
{
    switch (state) {
        case State::Usable: return QStringLiteral("ready");
        case State::AdapterPending: return QStringLiteral("adapter-pending");
        case State::DeploymentUnsupported: return QStringLiteral("deployment-unsupported");
    }
    return QStringLiteral("deployment-unsupported");
}

WindowsOcrSupport inspectWindowsOcrSupport()
{
    UINT32     length   = 0;
    LONG const probe    = ::GetCurrentPackageFullName(&length, nullptr);
    bool const packaged = probe == ERROR_INSUFFICIENT_BUFFER and length > 0;
    if (not packaged) return {.state = WindowsOcrSupport::State::DeploymentUnsupported, .packaged = false};
    // A packaged process would still need an adapter that this build does not ship. Reporting
    // DeploymentUnsupported here would be wrong; reporting Usable would be worse.
    return {.state = WindowsOcrSupport::State::AdapterPending, .packaged = true};
}

}
