/**
 * @file pet_package_fs.cpp
 * @brief Implementation of the file-system checks on a pet package: links, containment, counts and sizes.
 */

#include "app/pet/pet_package_fs.h"

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>

#include <QString>

#include "app/paths.h"
#include "core/domain/pet/pet_package.h"
#include "util/text.h"

namespace lens::app::pet {

namespace fs = std::filesystem;

using core::pet::Refusal;
using core::pet::Refused;

bool isReparsePoint(fs::path const& path)
{
    // std::filesystem::is_symlink does not report a junction, and a junction is the link that needs no privilege.
    auto const attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES and (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

fs::path resolvePackageFile(fs::path const& root, std::string const& relative)
{
    if (not core::pet::isContainedSpelling(relative)) throw Refused(Refusal::PathEscapes, "pet: a path leaves the package: " + relative);

    // Every component under the root is checked before it is followed: a junction in the middle is refused even when the
    // file at the end would have resolved back inside.
    auto const written   = toPath(QString::fromStdString(relative));
    auto       candidate = root;
    for (auto const& part : written) {
        if (part.empty() or part == ".") continue;
        candidate /= part;
        auto error = std::error_code{};
        if (fs::exists(candidate, error) and isReparsePoint(candidate))
            throw Refused(Refusal::PathEscapes, "pet: a link lies on the path to " + relative);
    }

    auto error = std::error_code{};
    if (not fs::is_regular_file(candidate, error)) throw Refused(Refusal::Unreadable, "pet: file missing: " + relative);

    // Last, the location the file really has: it must still be under the package once every link is followed.
    auto const resolved = fs::canonical(candidate, error);
    if (error) throw Refused(Refusal::Unreadable, "pet: cannot resolve " + relative);
    auto const base = fs::canonical(root, error);
    if (error) throw Refused(Refusal::Unreadable, "pet: cannot resolve the package folder");
    auto const inside = resolved.lexically_relative(base);
    if (inside.empty() or *inside.begin() == "..") throw Refused(Refusal::PathEscapes, "pet: " + relative + " resolves outside the package");
    return candidate;
}

void checkPackageTree(fs::path const& root)
{
    auto files   = std::size_t{0};
    auto bytes   = std::uint64_t{0};
    auto folders = std::vector<fs::path>{root};
    while (not folders.empty()) {
        auto const folder = folders.back();
        folders.pop_back();
        try {
            for (auto const& entry : fs::directory_iterator{folder}) {
                if (isReparsePoint(entry.path()))
                    throw Refused(Refusal::PathEscapes, "pet: a link inside the package: " + narrow(entry.path()));
                if (entry.is_directory()) {
                    folders.push_back(entry.path());
                    continue;
                }
                if (not entry.is_regular_file()) continue;

                if (++files > core::pet::kMaxPackageFiles) throw Refused(Refusal::TooLarge, "pet: more than the file limit");
                auto const size = entry.file_size();
                bytes += size;
                if (bytes > core::pet::kMaxPackageBytes) throw Refused(Refusal::TooLarge, "pet: the package is over 32 MB");

                auto const extension = util::asciiLower(entry.path().extension().string());
                if (extension == ".json" and size > core::pet::kMaxJsonBytes)
                    throw Refused(Refusal::TooLarge, "pet: a JSON file is over 256 KB: " + narrow(entry.path()));
                if (extension == ".png" and size > core::pet::kMaxPngBytes)
                    throw Refused(Refusal::TooLarge, "pet: a PNG is over 8 MB: " + narrow(entry.path()));
            }
        } catch (fs::filesystem_error const& e) {
            throw Refused(Refusal::Unreadable, std::string{"pet: cannot list a folder: "} + e.what());
        }
    }
}

} // namespace lens::app::pet
