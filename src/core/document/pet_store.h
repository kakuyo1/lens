#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/document/known_store.h"

/**
 * @file pet_store.h
 * @brief The PET section of the settings document: the chosen pet, the two switches, each pet's own size and outfit,
 *        and the reader's one saved window position (PHASE3 3.6, docs/adr/0096, docs/adr/0100).
 *
 * Like StatsStore, this binds to the document KnownStore owns and writes only its own key. It persists through
 * KnownStore::save(), so there is still one file and one writer.
 *
 * The size and the outfit belong to one pet each, keyed by id: choosing a pet returns it in the clothes it left
 * them in. The window position, the switches and the confirmed list are the reader's: the window is one place on
 * the desktop, so switching pets leaves it where it is.
 */

namespace lens::core::pet {

/// @brief The accessory worn in each slot, by id. An empty slot is nothing.
struct Outfit {
    std::optional<std::string> head;
    std::optional<std::string> face;
    std::optional<std::string> body;
};

/// @brief The smallest and largest size the slider offers, as a percentage of the way from ×1 to ×4 (PHASE3 3.1): 0 is
///        today's ×1, 100 today's ×4. The slider runs these whole-percent stops; the magnification between them is real.
constexpr int kMinSize = 0;
constexpr int kMaxSize = 100;

/// @return The percentage a package's whole-number 1..4 scale means to a reader who has never moved the slider. A
///         package's declared scale is that reader's fallback, so `scale` 3 reads as 67%.
inline int sizeFromScale(int scale)
{
    return static_cast<int>(std::lround((scale - 1) * 100.0 / 3.0));
}

/// @return The magnification a size draws, in [1,4]: 67 is ×3.01.
inline double magnificationOf(int size)
{
    return 1.0 + 3.0 * size / 100.0;
}

/// @brief The shipped pet a reader is given when the one they chose is gone or refused (docs/adr/0043).
inline constexpr char const* kDefaultPet = "dog";

/// @brief Where the window was left: the screen it sat on, by device name, and its offset from that screen's origin.
struct SavedPosition {
    std::string screen;
    int         x;
    int         y;
};

class PetStore final {
public:
    /// @param store The one owner of the settings document, which must outlive this object.
    explicit PetStore(KnownStore& store);

    /// @return The folder name of the pet the reader chose, or the default pet until they choose another.
    std::string pet() const;
    void        setPet(std::string const& id);

    /// @return The ids of the reader's packages they have confirmed. A confirmed package is not asked about again, even
    ///         after it was removed and put back.
    std::vector<std::string> confirmed() const;
    /// @brief Records that the reader confirmed the package `id`.
    void confirm(std::string const& id);

    /// @return Whether the reader turned the desktop pet on. Off by default.
    bool enabled() const;
    void setEnabled(bool on);

    /// @return Whether the window lets mouse events through. Off by default.
    bool passthrough() const;
    void setPassthrough(bool on);

    /// @param pet The pet the size belongs to, by folder name.
    /// @param fallback Returned while that pet has no chosen size, normally the package's declared scale as a
    ///                 percentage through sizeFromScale().
    /// @return The chosen size in kMinSize..kMaxSize.
    int size(std::string const& pet, int fallback) const;
    /// @brief Stores the size, clamped to kMinSize..kMaxSize.
    void setSize(std::string const& pet, int percent);

    /// @return The pet's worn accessories, as last saved.
    Outfit outfit(std::string const& pet) const;
    void   setOutfit(std::string const& pet, Outfit const& outfit);

    /// @return Where the reader left the window, or nothing before it was first moved. One spot serves every pet.
    std::optional<SavedPosition> position() const;
    void                         setPosition(SavedPosition const& at);

private:
    /// @return The one pet's own object under PET.pets, where its size and outfit live.
    SettingsSection entry(std::string const& pet) const;

    KnownStore& store_;
};

} // namespace lens::core::pet
