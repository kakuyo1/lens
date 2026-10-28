#pragma once

/**
 * @file running_instance.h
 * @brief The named mutex that tells the uninstaller a Lens is running.
 */

namespace lens::app {

/**
 * @brief Hold the running-instance mutex for the rest of the process.
 * @note Windows keeps a running executable's image locked, so an uninstaller that deletes around
 *       one leaves a half-removed install. The uninstaller refuses while this exists; the handle
 *       is never closed because process exit is what releases it.
 */
void holdRunningInstance();

}
