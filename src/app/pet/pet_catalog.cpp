/**
 * @file pet_catalog.cpp
 * @brief Implementation of scanPets: the two folders, the reserved shipped ids, and the refusal list.
 */

#include "app/pet/pet_catalog.h"

#include <algorithm>
#include <exception>
#include <system_error>
#include <vector>

#include <QString>

#include "app/paths.h"
#include "app/pet/pet_assets.h"
#include "util/log.h"

namespace lens::app::pet {

namespace fs = std::filesystem;

namespace {

using core::pet::Refusal;
using core::pet::Refused;

/// @return The folders directly under `root`, in name order. A missing or unreadable root holds none.
std::vector<fs::path> foldersIn(fs::path const& root)
{
    auto folders = std::vector<fs::path>{};
    auto error   = std::error_code{};
    auto it      = fs::directory_iterator{root, error};
    if (error) return folders;
    for (auto const end = fs::directory_iterator{}; it != end; it.increment(error)) {
        if (error) break;
        if (it->is_directory(error)) folders.push_back(it->path());
    }
    std::sort(folders.begin(), folders.end());
    return folders;
}

/// @return Whether two ids are the same once case is ignored. Windows folders are case-insensitive, so `Dog` is `dog`.
bool sameId(std::string const& a, std::string const& b)
{
    return QString::compare(QString::fromStdString(a), QString::fromStdString(b), Qt::CaseInsensitive) == 0;
}

/// @return The folder name for the refusal list: cleaned as a package's own text is, then cut to the name length so one
///         long name cannot stretch the list.
QString listedNameOf(fs::path const& folder)
{
    return plainText(widen(folder.filename())).left(core::pet::kMaxNameChars);
}

} // namespace

PetEntry const* PetCatalog::find(std::string_view id) const
{
    auto const found = std::find_if(pets.begin(), pets.end(), [id](PetEntry const& entry) { return entry.id == id; });
    return found == pets.end() ? nullptr : &*found;
}

PetCatalog scanPets(fs::path const& shippedRoot, fs::path const& installedRoot)
{
    auto catalog = PetCatalog{};

    auto reserved = std::vector<std::string>{};
    for (auto const& folder : foldersIn(shippedRoot)) {
        auto const id = narrow(folder.filename());
        reserved.push_back(id);
        try {
            auto const assets = PetAssets::load(folder, PetSource::Shipped);
            catalog.pets.push_back(PetEntry{id, assets.name, PetSource::Shipped, folder});
        } catch (std::exception const& e) {
            LENS_ERROR("the shipped pet {} is off: {}", id, e.what());
        }
    }

    for (auto const& folder : foldersIn(installedRoot)) {
        auto const id = narrow(folder.filename());
        try {
            auto const assets = PetAssets::load(folder, PetSource::Installed);
            auto const taken  = std::any_of(reserved.begin(), reserved.end(), [&id](std::string const& shipped) { return sameId(shipped, id); });
            if (taken) throw Refused(Refusal::IdTaken, "pet: " + id + " is the id of a shipped pet");
            catalog.pets.push_back(PetEntry{id, assets.name, PetSource::Installed, folder});
        } catch (Refused const& refusal) {
            LENS_WARN("the pet package {} is refused: {}", id, refusal.what());
            catalog.refused.push_back(RefusedPackage{listedNameOf(folder), refusal.reason()});
        } catch (std::exception const& e) {
            LENS_WARN("the pet package {} could not be read: {}", id, e.what());
            catalog.refused.push_back(RefusedPackage{listedNameOf(folder), Refusal::Unreadable});
        }
    }
    return catalog;
}

} // namespace lens::app::pet
