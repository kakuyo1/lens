/**
 * @file pet_store.cpp
 * @brief Implementation of PetStore: reads and writes the PET object of the settings document.
 */

#include "core/document/pet_store.h"

#include <algorithm>

#include "core/domain/pet/pet_package.h"

namespace lens::core::pet {

namespace {

/// @brief Carries a size stored before the unit changed over, once. The old `scale` key held a whole-number
///        magnification in the manifest's 1..4 range; converted to the percentage the new `size` key holds, and the old
///        key taken away, so the conversion never runs a second time and the document is never left ambiguous.
void migrateLegacySize(SettingsSection pet)
{
    if (pet.has("size") or not pet.has("scale")) return;
    auto const times = pet.value<int>("scale", 0);
    // Anything outside the manifest's range is a hand-edited document, not the old size.
    if (times < kMinScale or times > kMaxScale) return;
    pet.set<int>("size", sizeFromScale(times));
    pet.remove("scale");
}

/// @return The id worn in a slot, or nothing when the slot is bare. An id is never empty, so an
///         empty value is a slot that was cleared or never filled.
std::optional<std::string> wornId(SettingsSection const& accessories, const char* key)
{
    auto id = accessories.value<std::string>(key, {});
    if (id.empty()) return std::nullopt;
    return id;
}

/// @brief Write one slot: the id it carries, or null for a bare one.
void writeSlot(SettingsSection& accessories, const char* key, std::optional<std::string> const& id)
{
    if (id)
        accessories.set<std::string>(key, *id);
    else
        accessories.clear(key);
}

} // namespace

PetStore::PetStore(KnownStore& store)
    : store_(store)
{
    migrateLegacySize(store_.section("PET"));
}

std::string PetStore::pet() const
{
    return store_.section("PET").value<std::string>("pet", kDefaultPet);
}

void PetStore::setPet(std::string const& id)
{
    store_.section("PET").set<std::string>("pet", id);
}

std::vector<std::string> PetStore::confirmed() const
{
    return store_.section("PET").value<std::vector<std::string>>("confirmed", {});
}

void PetStore::confirm(std::string const& id)
{
    auto ids = confirmed();
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
    ids.push_back(id);
    store_.section("PET").set<std::vector<std::string>>("confirmed", ids);
}

bool PetStore::enabled() const
{
    return store_.section("PET").value<bool>("enabled", false);
}

void PetStore::setEnabled(bool on)
{
    store_.section("PET").set<bool>("enabled", on);
}

bool PetStore::passthrough() const
{
    return store_.section("PET").value<bool>("passthrough", false);
}

void PetStore::setPassthrough(bool on)
{
    store_.section("PET").set<bool>("passthrough", on);
}

int PetStore::size(int fallback) const
{
    return std::clamp(store_.section("PET").value<int>("size", fallback), kMinSize, kMaxSize);
}

void PetStore::setSize(int percent)
{
    store_.section("PET").set<int>("size", std::clamp(percent, kMinSize, kMaxSize));
}

Outfit PetStore::outfit() const
{
    auto const accessories = store_.section("PET").object("accessories");
    return Outfit{wornId(accessories, "head"), wornId(accessories, "face"), wornId(accessories, "body")};
}

void PetStore::setOutfit(Outfit const& outfit)
{
    auto accessories = store_.section("PET").object("accessories");
    writeSlot(accessories, "head", outfit.head);
    writeSlot(accessories, "face", outfit.face);
    writeSlot(accessories, "body", outfit.body);
}

std::optional<SavedPosition> PetStore::position() const
{
    auto const at = store_.section("PET").object("position");
    // All three or nothing: a half-written position is a document someone edited, not a place
    // the window was left.
    if (not at.has("screen") or not at.has("x") or not at.has("y")) return std::nullopt;
    return SavedPosition{at.value<std::string>("screen", {}), at.value<int>("x", 0), at.value<int>("y", 0)};
}

void PetStore::setPosition(SavedPosition const& at)
{
    auto position = store_.section("PET").object("position");
    position.set<std::string>("screen", at.screen);
    position.set<int>("x", at.x);
    position.set<int>("y", at.y);
}

} // namespace lens::core::pet
