/** @file scan_pipeline.h
 * @brief The process whitelist the hover lookup checks before it reads a window.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lens::capture {

/// @brief Whether a process may be read: its executable name must match a whitelist entry exactly.
/// @param process   The process's executable path or name; directories are ignored.
/// @param whitelist Lowercase executable names the settings allow.
/// @param own       True for this program's own process, which is never read.
/// @return Whether the process is allowed; an empty name or an own process is refused.
bool allowedProcess(std::string_view process, std::vector<std::string> const& whitelist, bool own);

}
