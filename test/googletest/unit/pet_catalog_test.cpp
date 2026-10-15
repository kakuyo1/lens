/**
 * @file pet_catalog_test.cpp
 * @brief The pet picker's catalogue, offline: shipped and installed pets, the refusal list, and the reserved shipped ids
 *        (docs/adr/0043). Also an opt-in run over packages made outside the repository.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <QString>

#include "app/pet/pet_catalog.h"

namespace {

using lens::app::pet::PetCatalog;
using lens::app::pet::PetSource;
using lens::app::pet::scanPets;
using lens::core::pet::Refusal;
using nlohmann::json;

std::filesystem::path shippedRoot()
{
    return std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet";
}

/// Copies the shipped dog into `installed/<folder>` with its id rewritten to `id` and, when given, a display name.
void installCopyOfDog(std::filesystem::path const& installed, std::string const& folder, std::string const& id, std::string const& name = {})
{
    auto const target = installed / folder;
    std::filesystem::copy(shippedRoot() / "dog", target, std::filesystem::copy_options::recursive);

    auto doc  = json::parse(std::ifstream{target / "pet.json"});
    doc["id"] = id;
    if (not name.empty()) doc["name"] = name;
    auto out = std::ofstream{target / "pet.json", std::ios::binary};
    out << doc.dump(2);
}

/// A scratch folder for the reader's pets, emptied first and removed when the test ends.
class ScratchInstalled {
public:
    explicit ScratchInstalled(std::string const& name)
        : root_{std::filesystem::temp_directory_path() / ("lens-pet-catalog-" + name)}
    {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }
    ~ScratchInstalled()
    {
        std::filesystem::remove_all(root_);
    }

    std::filesystem::path const& path() const
    {
        return root_;
    }

private:
    std::filesystem::path root_;
};

TEST(PetCatalog, ShippedPetsAreOfferedAsShipped)
{
    auto scratch = ScratchInstalled{"shipped-only"};

    auto const catalog = scanPets(shippedRoot(), scratch.path());

    auto const* dog = catalog.find("dog");
    ASSERT_NE(dog, nullptr);
    EXPECT_EQ(dog->source, PetSource::Shipped);
    EXPECT_EQ(dog->name, QStringLiteral("dog"));
    EXPECT_TRUE(catalog.refused.empty());
}

TEST(PetCatalog, AValidInstalledPackageIsOfferedWithItsOwnName)
{
    auto scratch = ScratchInstalled{"valid"};
    installCopyOfDog(scratch.path(), "lounge", "lounge", "Lounge");

    auto const catalog = scanPets(shippedRoot(), scratch.path());

    auto const* lounge = catalog.find("lounge");
    ASSERT_NE(lounge, nullptr);
    EXPECT_EQ(lounge->source, PetSource::Installed);
    EXPECT_EQ(lounge->name, QStringLiteral("Lounge"));
    EXPECT_EQ(lounge->directory, scratch.path() / "lounge");
}

TEST(PetCatalog, AnInstalledPackageWithTheIdOfAShippedPetIsIdTaken)
{
    auto scratch = ScratchInstalled{"taken"};
    installCopyOfDog(scratch.path(), "dog", "dog");

    auto const catalog = scanPets(shippedRoot(), scratch.path());

    auto const* shipped = catalog.find("dog");
    ASSERT_NE(shipped, nullptr);
    EXPECT_EQ(shipped->source, PetSource::Shipped) << "a shipped pet is never replaced";
    ASSERT_EQ(catalog.refused.size(), 1u);
    EXPECT_EQ(catalog.refused.front().id, QStringLiteral("dog"));
    EXPECT_EQ(catalog.refused.front().reason, Refusal::IdTaken);
}

TEST(PetCatalog, AnInstalledPackageThatFailsIsListedWithItsReasonAndNotOffered)
{
    auto scratch = ScratchInstalled{"refused"};
    installCopyOfDog(scratch.path(), "broken", "broken");
    auto doc      = json::parse(std::ifstream{scratch.path() / "broken" / "pet.json"});
    doc["format"] = 2;
    auto out      = std::ofstream{scratch.path() / "broken" / "pet.json", std::ios::binary};
    out << doc.dump(2);
    out.close();

    auto const catalog = scanPets(shippedRoot(), scratch.path());

    EXPECT_EQ(catalog.find("broken"), nullptr);
    ASSERT_EQ(catalog.refused.size(), 1u);
    EXPECT_EQ(catalog.refused.front().id, QStringLiteral("broken"));
    EXPECT_EQ(catalog.refused.front().reason, Refusal::Format);
}

TEST(PetCatalog, AFolderWithNoPetJsonIsUnreadable)
{
    auto scratch = ScratchInstalled{"empty-folder"};
    std::filesystem::create_directories(scratch.path() / "notes");

    auto const catalog = scanPets(shippedRoot(), scratch.path());

    ASSERT_EQ(catalog.refused.size(), 1u);
    EXPECT_EQ(catalog.refused.front().reason, Refusal::Unreadable);
}

TEST(PetCatalog, AMissingInstalledFolderHoldsNoPackages)
{
    auto scratch = ScratchInstalled{"missing"};

    auto const catalog = scanPets(shippedRoot(), scratch.path() / "not-there");

    EXPECT_NE(catalog.find("dog"), nullptr);
    EXPECT_TRUE(catalog.refused.empty());
    for (auto const& pet : catalog.pets)
        EXPECT_EQ(pet.source, PetSource::Shipped);
}

TEST(PetCatalog, ExternalPackagesMadeOutsideTheRepositoryAreAcceptedOrRefusedAsWritten)
{
    // Opt-in. LENS_PET_PACKAGES names a folder of pet packages made by someone else, such as a third-party author's
    // output, and the scan runs over it as the reader's folder. The package white_fox must be offered and white_fox_evil
    // must be refused. Unset, the check is skipped.
    auto const* folder = std::getenv("LENS_PET_PACKAGES");
    if (folder == nullptr) GTEST_SKIP() << "set LENS_PET_PACKAGES to a folder of packages to run this check";

    auto const catalog = scanPets(shippedRoot(), folder);

    auto const* normal = catalog.find("white_fox");
    ASSERT_NE(normal, nullptr) << "white_fox was not offered";
    EXPECT_EQ(normal->source, PetSource::Installed);
    EXPECT_EQ(catalog.find("white_fox_evil"), nullptr) << "the malicious package was offered";

    auto refusedEvil = false;
    for (auto const& refused : catalog.refused) {
        if (refused.id == QStringLiteral("white_fox_evil")) refusedEvil = true;
    }
    EXPECT_TRUE(refusedEvil) << "white_fox_evil was not listed as refused";
}

TEST(PetCatalog, AFolderNameIsCleanedOfInvisibleCharactersBeforeItIsListed)
{
    auto scratch = ScratchInstalled{"cleaned-listed"};
    std::filesystem::create_directories(scratch.path() / std::filesystem::path{std::wstring{L"lo"} + wchar_t{0x200B} + L"unge"});

    auto const catalog = scanPets(shippedRoot(), scratch.path());

    ASSERT_EQ(catalog.refused.size(), 1u);
    EXPECT_EQ(catalog.refused.front().id, QStringLiteral("lounge"));
}

} // namespace
