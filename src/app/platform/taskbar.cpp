/**
 * @file taskbar.cpp
 * @brief The shell's appbar state behind the pet's taskbar trip.
 */

#include "app/platform/taskbar.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>
#include <shellapi.h>

namespace lens::app {

bool taskbarAutoHides()
{
    auto data   = APPBARDATA{};
    data.cbSize = sizeof(data);
    // ABM_GETSTATE returns ABS_AUTOHIDE and ABS_ALWAYSONTOP together. The call has no separate failure value:
    // zero is the ordinary "visible, on top" answer.
    auto const state = SHAppBarMessage(ABM_GETSTATE, &data);
    return (state & ABS_AUTOHIDE) != 0;
}

} // namespace lens::app
