#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <QString>

#include "app/pet/pet_assets.h"
#include "core/domain/pet/pet_package.h"

/**
 * @file pet_catalog.h
 * @brief Which pets the settings offer: the ones shipped with Lens, and the packages the reader installed (docs/adr/0043).
 *
 * Each entry into the extensions section compares the folders' stamp (scanStamp) and scans again only when it moved,
 * so a package put there is seen without a restart while an unchanged pair of folders costs no scan at all
 * (docs/adr/0169). Every pet is checked whole by PetAssets::load, the same check the pet runs on. A package that
 * fails is listed with its reason, and is not offered.
 */

namespace lens::app::pet {

/// @brief One pet the picker offers, checked whole.
struct PetEntry {
    /// The folder name, which is the id the settings save and the picker chooses by.
    std::string id;
    /// The name to show: the package's own, cleaned, or the id when the package has none.
    QString               name;
    PetSource             source;
    std::filesystem::path directory;
};

/// @brief An installed package that was refused, and why. The reader sees the folder name and the reason, nothing else
///        the package wrote.
struct RefusedPackage {
    /// The folder name as the reader sees it: cleaned of control and format characters, and cut to the name length limit.
    QString            id;
    core::pet::Refusal reason;
};

/// @brief What one scan found.
struct PetCatalog {
    /// The shipped pets in folder order, then the installed ones in folder order.
    std::vector<PetEntry>       pets;
    std::vector<RefusedPackage> refused;

    /// @return The pet with this id, or nullptr when the catalogue does not hold one.
    PetEntry const* find(std::string_view id) const;
};

/**
 * @brief Scans the shipped folder and the reader's folder, and checks every pet in both.
 *
 * A shipped pet that fails is logged and left out, which turns the pet off as before. An installed package is refused
 * with its reason when it fails, or with IdTaken when its folder name is a shipped pet's id, compared without case.
 * Shipped folder names stay reserved even when the pet under them fails, so a package cannot take the name.
 *
 * @param shippedRoot The data/pet folder that came with the install.
 * @param installedRoot The reader's pets folder, %APPDATA%\Lens\pets. A missing one holds no packages.
 * @return The pets offered and the packages refused.
 */
PetCatalog scanPets(std::filesystem::path const& shippedRoot, std::filesystem::path const& installedRoot);

/// @brief The folder state a refresh compares: each root's last-write time, each folder under them, and each
///        document a package is read from. Equal stamps mean a scan would answer what the last one answered.
using ScanStamp = std::vector<std::filesystem::file_time_type>;

/**
 * @brief Takes the stamp held between entries into the extensions section.
 *
 * Adding, removing or renaming a folder stamps its parent, and editing a document in place stamps the document, so
 * anything that can change the scan's answer moves the stamp. A sprite redrawn over itself moves neither; the
 * package is read whole again whenever its pet loads, which is where changed art has to hold up anyway.
 *
 * @param shippedRoot The data/pet folder that came with the install.
 * @param installedRoot The reader's pets folder, %APPDATA%\Lens\pets. A missing one stamps as it is.
 * @return The stamp, in folder order so two of the same tree compare equal.
 */
ScanStamp scanStamp(std::filesystem::path const& shippedRoot, std::filesystem::path const& installedRoot);

} // namespace lens::app::pet
