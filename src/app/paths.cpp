/**
 * @file paths.cpp
 * @brief The path helpers with a body: see paths.h for why they are not in main.cpp.
 */

#include "paths.h"

#include <QStandardPaths>

#include <windows.h>
#include <shlobj.h>

#include "util/log.h"

namespace lens::app {
namespace {

/// The profile's leaf directory. It must match the applicationName main() sets, which is what
/// settingsPath() reaches the same directory through.
constexpr wchar_t kProfileLeaf[] = L"Lens";

/// The leaf beside the executable that marks an installed build; the install tree carries data/.
constexpr wchar_t kDataLeaf[] = L"data";

/// @brief The reader's profile directory, `%APPDATA%\Lens`, without a QCoreApplication to ask.
///
/// QStandardPaths answers the same directory only once main() has built the application object
/// and set its name, and logDirectory() runs before that -- the crash handler is installed first
/// so a death inside QApplication leaves its artifacts too.
///
/// @return The directory, or an empty one when Windows refuses the answer; the callers fall back
///         to the working directory rather than fail.
std::filesystem::path profileDir()
{
    wchar_t*   roaming = nullptr;
    const auto hr      = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming);
    if (hr != S_OK || roaming == nullptr)
        return {};
    const std::filesystem::path profile = std::filesystem::path{roaming} / kProfileLeaf;
    CoTaskMemFree(roaming);
    return profile;
}

}

std::filesystem::path settingsPath(const std::filesystem::path& legacy)
{
    const std::filesystem::path target =
        toPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)) / "settings.json";

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        LENS_WARN("settings directory {} could not be created: {}", narrow(target.parent_path()), ec.message());
        return target;
    }

    if (!std::filesystem::exists(target, ec) && std::filesystem::exists(legacy, ec)) {
        std::filesystem::copy_file(legacy, target, ec);
        if (ec)
            LENS_WARN("the settings document was not carried over from {}: {}", narrow(legacy), ec.message());
        else
            LENS_INFO("settings carried over from {} to {}", narrow(legacy), narrow(target));
    }
    return target;
}

bool installedBuild()
{
    wchar_t     executable[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return false;
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path{executable, executable + length}.parent_path() / kDataLeaf, ec);
}

std::filesystem::path logDirectory(bool installedBuild)
{
    if (!installedBuild)
        return std::filesystem::path{lens::log::kLogDirName};
    const std::filesystem::path profile = profileDir();
    if (profile.empty())
        return std::filesystem::path{lens::log::kLogDirName}; // No profile to hold it: log::init() warns if the working directory cannot be written either.
    return profile / lens::log::kLogDirName;
}

}
