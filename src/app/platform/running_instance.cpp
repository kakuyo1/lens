/**
 * @file running_instance.cpp
 * @brief The Win32 mutex behind holdRunningInstance().
 */

#include "app/platform/running_instance.h"

#include <windows.h>

namespace lens::app {
namespace {

HANDLE runningInstance = nullptr;

}

void holdRunningInstance()
{
    if (runningInstance != nullptr) return;
    // The name is mirrored by LensRunningMutex in installer/lens.iss; both must agree.
    runningInstance = CreateMutexW(nullptr, FALSE, L"Lens.Desktop.Running");
}

}
