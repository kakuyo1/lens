#pragma once

/**
 * @file settings_table.h
 * @brief Every setting the facade owns, one row each: the key, the type it is stored as, the value a
 *        document that names nothing reads as, the clamp, the signal its write raises, and whether a
 *        sync carries it.
 *
 * One row per setting is what the ten sites that used to restate it read instead (docs/adr/0083).
 *
 * Deliberately Qt-free: sync_state.cpp builds the whitelist a sync carries out of these rows, and
 * core has no Qt. What a key means to a surface -- a label, a preview, an option list -- is not here.
 *
 * Four keys are absent on purpose, because their write is a command rather than a value: `API-KEY`
 * (a secret that is never echoed), and `PROVIDER`, `MODEL` and `URL`, which the model catalogue owns
 * and writes as one decision about the service. `ocrHotkey` is absent for the same reason: the shell
 * has to take the combination before the document may record it. `reviewFrequency` lives in the
 * review store's own object, not at the document's top level.
 */

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace lens::core::settings {

/// @brief How a key is stored, and therefore how it is read, written and compared.
enum class Kind : std::uint8_t {
    Toggle,
    Integer, ///< A whole number in a closed range; an enumerated setting's range is its enum's.
    Choice,  ///< One of a fixed set of strings; the members are the row's.
    Text,    ///< Free text: a language code, or a theme's packed value.
    Color,   ///< #rrggbb.
    Font,    ///< A family this machine has, or empty for the face the application installed.
};

/// @brief What a write to a key raises, and -- for the three owners that hold their value elsewhere --
///        where the write goes. Exhaustive on purpose: a new row has to pick one.
enum class Signal : std::uint8_t {
    Settings,        ///< Stored through the section; settingsChanged().
    Language,        ///< Stored through the section; settingsChanged(), statsChanged() and uiLanguageChanged().
    Logged,          ///< Stored through the section; settingsChanged(), and a line in the log.
    Glass,           ///< Stored through the section; settingsChanged(), and the backdrop takes the new tier.
    Level,           ///< KnownStore holds it: settingsChanged().
    ExplanationLang, ///< KnownStore holds it: settingsChanged() and statsChanged().
    Capture,         ///< CaptureDuty writes it, because the OCR engine or the scan changes with it.
};

/// @brief The value a document that names nothing reads as. One of the three, and @c kind says which.
struct Fallback {
    constexpr Fallback(bool const value)
        : kind(Kind::Toggle), toggle(value)
    {}
    constexpr Fallback(int const value)
        : kind(Kind::Integer), number(value)
    {}
    constexpr Fallback(char const* const value)
        : kind(Kind::Text), text(value)
    {}
    constexpr Fallback(std::string_view const value)
        : kind(Kind::Text), text(value)
    {}

    Kind             kind{Kind::Toggle};
    bool             toggle{false};
    int              number{0};
    std::string_view text{};
};

/// @brief One setting, as the whole application reads and writes it.
struct Row {
    std::string_view key;
    Kind             kind;
    Signal           signal;
    Fallback         fallback;
    int              low{0};  ///< The clamp's floor; Integer rows only.
    int              high{0}; ///< The clamp's ceiling; Integer rows only.
    bool             synced{false};
    /// Whether restore-defaults puts the key back. The interface language is the one row that says
    /// no: it is the reader's, not the settings panel's (docs/adr-0062).
    bool                              restorable{true};
    std::span<std::string_view const> members{}; ///< Choice rows: the values the key accepts.
};

/// The three sets of members a Choice row names. They are the stored values, so a member is what the
/// document holds, not what the surface labels it.
inline constexpr std::string_view kPlacements[]{"above", "below"};
inline constexpr std::string_view kBarStyles[]{"bar", "tab", "hand", "tag", "fan"};
inline constexpr std::string_view kClipboardPolicies[]{"topmost", "silent"};
/// The two ways a manual capture may leave the machine, and the local engines the first release
/// offers. `windows` stays a stored member so a future packaged build can add it without a
/// migration; this build reports it as unavailable rather than silently changing the choice.
inline constexpr std::string_view kOcrModes[]{"local", "online"};
inline constexpr std::string_view kOcrLocalEngines[]{"auto", "windows", "tesseract", "rapidocr"};
inline constexpr std::string_view kOcrLocalLanguages[]{"en", "zh-Hans", "zh-Hans+en"};
inline constexpr std::string_view kOcrOnlineLanguages[]{"eng", "auto"};
/// The mother tongue the bubble's peek answers in: the catalog's own languages without English,
/// which is the one value under which the peek would have nothing to peek at (docs/adr/0112).
inline constexpr std::string_view kMotherTongues[]{"zh", "zh-Hant", "es", "ja"};

/**
 * @brief The table, in the order a restore walks it.
 *
 * The order of the synced rows is also the order a sync document lists them; nothing depends on it,
 * because a JSON object is keyed, but a stable order keeps two dumps comparable by eye.
 */
inline constexpr Row kRows[]{
    // Learning: the difficulty, the language the answer is asked in, the language the held peek
    // answers in, and what comes with it.
    {.key = "level", .kind = Kind::Integer, .signal = Signal::Level, .fallback = 2, .low = 0, .high = 7, .synced = true},
    {.key = "explanationLang", .kind = Kind::Text, .signal = Signal::ExplanationLang, .fallback = "en", .synced = true},
    {.key = "motherTongue", .kind = Kind::Choice, .signal = Signal::Settings, .fallback = "zh", .synced = true, .members = kMotherTongues},
    {.key = "multiSense", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = false, .synced = true},
    {.key = "etymology", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = false, .synced = true},
    {.key = "example", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = false, .synced = true},
    // What the card draws from an answer it already has. These are display switches: the
    // request and the cache are the same either way, and the default is on because the blocks
    // predate the switches -- the part of speech is the one exception, drawn for the first time
    // with its switch and defaulted on to match the prototype the others were measured against.
    {.key = "ipa", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = true, .synced = true},
    {.key = "pos", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = true, .synced = true},
    {.key = "signpost", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = true, .synced = true},
    {.key = "collocations", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = true, .synced = true},
    {.key = "pictures", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = true, .synced = true},
    // The reader's own interface language: stored and synced, but not the panel's to restore.
    {.key = "uiLanguage", .kind = Kind::Text, .signal = Signal::Language, .fallback = "zh", .synced = true, .restorable = false},

    // Appearance: the theme, the face, the pace, and the glass under the cards (DESIGN.md 3.1c, 3.1d).
    // A custom theme's colours are packed into this one value, so they travel with it.
    {.key = "theme", .kind = Kind::Text, .signal = Signal::Settings, .fallback = "light", .synced = true},
    {.key = "font", .kind = Kind::Font, .signal = Signal::Settings, .fallback = ""},
    {.key = "animationsEnabled", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = true, .synced = true},
    {.key = "revealSpeed", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 3, .low = 1, .high = 5, .synced = true},
    // The four tiers of GlassQuality, numbered as that enum numbers them (src/app/glass_settings.h).
    {.key = "glassQuality", .kind = Kind::Integer, .signal = Signal::Glass, .fallback = 0, .low = 0, .high = 3, .synced = true},
    {.key = "frostedBlur", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 10, .low = 0, .high = 100, .synced = true},
    {.key = "frostedSaturation", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 120, .low = 0, .high = 200, .synced = true},
    {.key = "frostedRim", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 20, .low = 0, .high = 100, .synced = true},
    {.key = "liquidRefraction", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 20, .low = 0, .high = 40, .synced = true},
    {.key = "liquidBlur", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 10, .low = 0, .high = 100, .synced = true},
    {.key = "liquidRim", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 10, .low = 0, .high = 100, .synced = true},
    {.key = "liquidTextColor", .kind = Kind::Color, .signal = Signal::Settings, .fallback = "#101319", .synced = true},
    // The four active levels of the year graph, as a place in the twelve hues that panel offers
    // (UI-SPEC.md 4.8b). An index into the panel's own list rather than an angle, numbered the way
    // glassQuality is numbered: the list is the authority on what a number means, and it is the
    // panel that reads it. The pick outlives the panel, so it lives here rather than in the window.
    {.key = "yearHueIndex", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 4, .low = 0, .high = 11, .synced = true},

    // The card: the size its answer is read at (UI-SPEC.md 4.4), and the rest of its behaviour.
    {.key = "bubbleFontSize", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 13, .low = 12, .high = 18},
    {.key = "bubbleDismissSeconds", .kind = Kind::Integer, .signal = Signal::Settings, .fallback = 5, .low = 1, .high = 30, .synced = true},
    {.key = "bubblePlacement", .kind = Kind::Choice, .signal = Signal::Settings, .fallback = "above", .synced = true, .members = kPlacements},
    // Not a reader setting: the card writes it after it has shown the verdict row's one-time
    // sentence, so the sentence does not return on every bubble (UI-SPEC.md 4.3).
    {.key = "verdictHintSeen", .kind = Kind::Toggle, .signal = Signal::Settings, .fallback = false},
    {.key = "clipboardPolicy", .kind = Kind::Choice, .signal = Signal::Logged, .fallback = "topmost", .members = kClipboardPolicies},
    // The action bar's form (UI-SPEC.md 4.10, docs/adr/0056).
    {.key = "selectionBarStyle", .kind = Kind::Choice, .signal = Signal::Settings, .fallback = "bar", .synced = true, .members = kBarStyles},

    // Capture: what may start a capture, and where the OCR engine is.
    {.key = "selectionCapture", .kind = Kind::Toggle, .signal = Signal::Logged, .fallback = true},
    {.key = "ocrCapture", .kind = Kind::Toggle, .signal = Signal::Capture, .fallback = false},
    {.key = "autoScan", .kind = Kind::Toggle, .signal = Signal::Capture, .fallback = false},
    {.key = "minimumWordLength", .kind = Kind::Integer, .signal = Signal::Capture, .fallback = 3, .low = 2, .high = 5, .synced = true},
    {.key = "scanWhitelist", .kind = Kind::Text, .signal = Signal::Capture, .fallback = "chrome.exe;msedge.exe;firefox.exe;acrord32.exe"},
    {.key = "tesseractExecutable", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    {.key = "tesseractDataDirectory", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    // The user-installed RapidOCR distribution. The trust digest binds the explicit detection to
    // the executable and the selected model files; clearing either path clears it (ADR 0132).
    {.key = "rapidOcrExecutable", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    {.key = "rapidOcrModelsDirectory", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    {.key = "rapidOcrTrust", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    // The local OCR configuration. None of it syncs: paths and a future online endpoint belong to
    // this machine, and a credential is not a row at all (docs/adr/0130).
    {.key = "ocrConfigVersion", .kind = Kind::Integer, .signal = Signal::Capture, .fallback = 1, .low = 0, .high = 1000, .restorable = false},
    {.key = "ocrMode", .kind = Kind::Choice, .signal = Signal::Capture, .fallback = "local", .members = kOcrModes},
    {.key = "ocrLocalEngine", .kind = Kind::Choice, .signal = Signal::Capture, .fallback = "auto", .members = kOcrLocalEngines},
    {.key = "ocrLocalLanguage", .kind = Kind::Choice, .signal = Signal::Capture, .fallback = "en", .members = kOcrLocalLanguages},
    // The online half. The key is deliberately not a row: it is a secret, written by its own
    // command and never echoed into a projection (docs/adr/0130).
    {.key = "ocrOnlineProvider", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    {.key = "ocrOnlineEndpoint", .kind = Kind::Text, .signal = Signal::Capture, .fallback = ""},
    {.key = "ocrOnlineLanguage", .kind = Kind::Choice, .signal = Signal::Capture, .fallback = "eng", .members = kOcrOnlineLanguages},
};

/**
 * @brief Find the row a key names.
 * @param key The document key, as the projection hands it out.
 * @return The row, or nullptr when the table does not own the key.
 */
constexpr Row const* find(std::string_view const key)
{
    for (Row const& row : kRows)
        if (row.key == key)
            return &row;
    return nullptr;
}

/**
 * @brief Whether a Choice row takes @p value.
 * @return True for a row with no members, which is every row that is not a Choice.
 */
constexpr bool accepts(Row const& row, std::string_view const value)
{
    for (std::string_view const member : row.members)
        if (member == value)
            return true;
    return false;
}

/// @return Every key a sync carries, in the table's order.
inline std::vector<std::string_view> syncedKeys()
{
    std::vector<std::string_view> keys;
    for (Row const& row : kRows)
        if (row.synced)
            keys.push_back(row.key);
    return keys;
}

/// @brief What a table is allowed to look like, checked at compile time below.
constexpr bool tableIsSound()
{
    for (std::size_t index = 0; index < std::size(kRows); ++index) {
        Row const& row = kRows[index];
        // A default has to be of the row's own type, or a read would hand back the wrong shape.
        const bool fallbackFits = row.fallback.kind == row.kind or
                                  ((row.kind == Kind::Choice or row.kind == Kind::Color or row.kind == Kind::Font) and
                                   row.fallback.kind == Kind::Text);
        if (not fallbackFits)
            return false;
        if (row.kind == Kind::Integer and row.low > row.high)
            return false;
        if (row.kind == Kind::Choice and row.members.empty())
            return false;
        if (row.kind != Kind::Choice and not row.members.empty())
            return false;
        if (row.key.empty())
            return false;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
            if (kRows[earlier].key == row.key)
                return false; // one key, one row
    }
    return true;
}

static_assert(tableIsSound(), "every settings row has to agree with itself: one key, one type, one clamp");

} // namespace lens::core::settings
