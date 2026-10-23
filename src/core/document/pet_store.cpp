/**
 * @file pet_store.cpp
 * @brief Implementation of PetStore: reads and writes the PET object of the settings document.
 */

#include "core/document/pet_store.h"

#include <algorithm>

#include "core/domain/pet/pet_package.h"

namespace lens::core::pet {

namespace {

/// @brief One-time migration of a size stored under the old unit. The `scale` key held a whole-number
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

/// @brief One-time move of the flat, every-pet-the-same values into the chosen pet's own entry. The flat keys
///        belonged to whatever pet was on, which is the one the document names; taking them away means the move
///        never runs twice and the document is never left ambiguous. A key the pet already owns keeps its own value,
///        which only a hand-edited document could arrange.
void migrateLegacyPetState(SettingsSection pet)
{
    if (not(pet.has("size") or pet.has("position") or pet.has("accessories"))) return;
    auto const& id  = pet.value<std::string>("pet", kDefaultPet);
    auto        own = pet.object("pets").object(id);

    if (pet.has("size") and not own.has("size")) own.set<int>("size", pet.value<int>("size", 0));
    pet.remove("size");

    auto const from = pet.object("position");
    auto       to   = own.object("position");
    // All three or nothing, as position() reads: a half-written place is an edited document, not a nest.
    if (from.has("screen") and from.has("x") and from.has("y") and not to.has("screen")) {
        to.set<std::string>("screen", from.value<std::string>("screen", {}));
        to.set<int>("x", from.value<int>("x", 0));
        to.set<int>("y", from.value<int>("y", 0));
    }
    pet.remove("position");

    auto const worn = pet.object("accessories");
    auto       kept = own.object("accessories");
    for (auto const* slot : {"head", "face", "body"}) {
        if (worn.has(slot) and not kept.has(slot)) kept.set<std::string>(slot, worn.value<std::string>(slot, {}));
    }
    pet.remove("accessories");
}

} // namespace

PetStore::PetStore(KnownStore& store)
    : store_(store)
{
    auto pet = store_.section("PET");
    migrateLegacySize(pet);
    migrateLegacyPetState(pet);
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

int PetStore::size(std::string const& pet, int fallback) const
{
    return std::clamp(entry(pet).value<int>("size", fallback), kMinSize, kMaxSize);
}

void PetStore::setSize(std::string const& pet, int percent)
{
    entry(pet).set<int>("size", std::clamp(percent, kMinSize, kMaxSize));
}

Outfit PetStore::outfit(std::string const& pet) const
{
    auto const& accessories = entry(pet).object("accessories");
    return Outfit{wornId(accessories, "head"), wornId(accessories, "face"), wornId(accessories, "body")};
}

void PetStore::setOutfit(std::string const& pet, Outfit const& outfit)
{
    auto accessories = entry(pet).object("accessories");
    writeSlot(accessories, "head", outfit.head);
    writeSlot(accessories, "face", outfit.face);
    writeSlot(accessories, "body", outfit.body);
}

std::optional<SavedPosition> PetStore::position(std::string const& pet) const
{
    auto const& at = entry(pet).object("position");
    // All three or nothing: a half-written position is a document someone edited, not a place
    // the window was left.
    if (not at.has("screen") or not at.has("x") or not at.has("y")) return std::nullopt;
    return SavedPosition{at.value<std::string>("screen", {}), at.value<int>("x", 0), at.value<int>("y", 0)};
}

void PetStore::setPosition(std::string const& pet, SavedPosition const& at)
{
    auto position = entry(pet).object("position");
    position.set<std::string>("screen", at.screen);
    position.set<int>("x", at.x);
    position.set<int>("y", at.y);
}

SettingsSection PetStore::entry(std::string const& pet) const
{
    return store_.section("PET").object("pets").object(pet);
}

} // namespace lens::core::pet
