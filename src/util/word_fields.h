#pragma once

#include <cstdint>
#include <string_view>

/**
 * @file word_fields.h
 * @brief The fields of one word answer, named once for the wire, the stored entry and the card.
 *
 * The response schema (`data/llm/response.word.schema.json`), the stored entry
 * (`core::WordCache`) and the bubble payload each used to spell these fields for themselves, so
 * every added field was copied between them by hand and the streamed preview drifted from the
 * whole-answer check. This table is the one list a field is added to: the wire reader, the store's
 * codec and the app module that builds the card all walk it.
 *
 * It sits in `util` because `core` and `llm` are peers and neither may include the other
 * (docs/adr/0085). It carries the field list and the few facts each shape walks it with: the key,
 * the value's kind, whether a stored entry requires it, whether a preview may draw it, and which
 * switch hides it on the card. The rules that cannot be a row -- the schema's own required list,
 * the cache's language-dependent translation check, the card's language choice -- stay in their
 * own modules.
 *
 * `word` (the echoed title) and `senses` (the array these rows repeat inside) are structure, not
 * rows: the parser stamps the first and folds the second.
 */

namespace lens::word {

/// @brief Which half of an answer a row belongs to.
enum class Scope : std::uint8_t {
    Answer, ///< Once per word answer.
    Sense,  ///< Once per entry in an answer's `senses` array.
};

/// @brief The field's identity, independent of which record carries it.
enum class Slot : std::uint8_t {
    Ipa,
    Pos,
    En,
    Zh,
    Translation,
    Etymology,
    Example,
    Signpost,
    Collocations,
    Imageable,
    ImageQuery,
};

/// @brief The JSON shape the value has on the wire and in the store.
enum class ValueKind : std::uint8_t {
    Text,     ///< One string.
    Flag,     ///< One boolean.
    TextList, ///< An array of strings.
};

/// @brief What a stored entry does when a value is not the row's kind.
enum class WrongType : std::uint8_t {
    Drop,   ///< The entry is not readable; the next lookup asks the model again.
    Ignore, ///< Advisory fields: the value is dropped and the definition survives.
};

/// @brief The card block a field is drawn as, and therefore the setting that can hide it.
enum class Block : std::uint8_t {
    None, ///< Drawn with no switch of its own.
    Ipa,
    Pos,
    Signpost,
    Collocations,
    Pictures, ///< The paired `imageable` / `imageQuery`; one switch for both.
    Etymology,
    Example,
};

/// @brief One field, in every shape that carries it.
struct Field {
    Slot             slot  = Slot::Ipa;
    Scope            scope = Scope::Answer;
    std::string_view key{}; ///< Wire key, stored key and card key: the same word in all three.
    ValueKind        kind = ValueKind::Text;
    /// Whether a stored entry must carry the key. The wire's own required list is the schema's.
    bool required = false;
    /// Whether a stored entry's text must not be blank. `ipa` is required but may be empty.
    bool nonEmpty = false;
    /// The example: absent means "not bought", empty means "the model had none" (docs/adr/0033).
    bool triState = false;
    /// Whether a streaming preview may draw it before the whole answer has been checked.
    bool preview = true;
    /// What a stored value of the wrong JSON type does to the entry.
    WrongType wrongType = WrongType::Drop;
    /// Whether the stored codec writes the key even when its value is empty.
    bool alwaysWrite = false;
    /// The switch the card reads before it draws the block; `None` for a field with no block.
    Block block = Block::None;
};

inline constexpr Field kFields[]{
    // The answer itself.
    {.slot = Slot::Ipa, .scope = Scope::Answer, .key = "ipa", .kind = ValueKind::Text, .required = true, .alwaysWrite = true, .block = Block::Ipa},
    {.slot = Slot::Pos, .scope = Scope::Answer, .key = "pos", .kind = ValueKind::Text, .block = Block::Pos},
    {.slot = Slot::En, .scope = Scope::Answer, .key = "en", .kind = ValueKind::Text, .required = true, .nonEmpty = true, .alwaysWrite = true},
    {.slot = Slot::Zh, .scope = Scope::Answer, .key = "zh", .kind = ValueKind::Text, .required = true, .nonEmpty = true, .alwaysWrite = true},
    {.slot = Slot::Translation, .scope = Scope::Answer, .key = "translation", .kind = ValueKind::Text},
    {.slot = Slot::Etymology, .scope = Scope::Answer, .key = "etymology", .kind = ValueKind::Text, .block = Block::Etymology},
    // ponytail: English whatever the explanation language, so es and ja readers get an English
    // sentence. Upgrade: ask for it in the explanation language, as the origin does.
    {.slot = Slot::Example, .scope = Scope::Answer, .key = "example", .kind = ValueKind::Text, .triState = true, .block = Block::Example},
    {.slot = Slot::Signpost, .scope = Scope::Answer, .key = "signpost", .kind = ValueKind::Text, .block = Block::Signpost},
    {.slot = Slot::Collocations, .scope = Scope::Answer, .key = "collocations", .kind = ValueKind::TextList, .block = Block::Collocations},
    // The picture advice is advisory: a malformed value never costs the definition around it.
    // A preview does not draw it, because a picture search bought from a half-written answer
    // may run against a query the finished answer would have corrected.
    {.slot = Slot::Imageable, .scope = Scope::Answer, .key = "imageable", .kind = ValueKind::Flag, .preview = false, .wrongType = WrongType::Ignore, .block = Block::Pictures},
    {.slot = Slot::ImageQuery, .scope = Scope::Answer, .key = "imageQuery", .kind = ValueKind::Text, .preview = false, .wrongType = WrongType::Ignore, .block = Block::Pictures},

    // The same fields once per sense. A sense's three text lines are always written, and its
    // picture advice follows the answer's rule.
    {.slot = Slot::En, .scope = Scope::Sense, .key = "en", .kind = ValueKind::Text, .required = true, .nonEmpty = true, .alwaysWrite = true},
    {.slot = Slot::Zh, .scope = Scope::Sense, .key = "zh", .kind = ValueKind::Text, .required = true, .nonEmpty = true, .alwaysWrite = true},
    {.slot = Slot::Translation, .scope = Scope::Sense, .key = "translation", .kind = ValueKind::Text, .alwaysWrite = true},
    {.slot = Slot::Imageable, .scope = Scope::Sense, .key = "imageable", .kind = ValueKind::Flag, .preview = false, .wrongType = WrongType::Ignore, .block = Block::Pictures},
    {.slot = Slot::ImageQuery, .scope = Scope::Sense, .key = "imageQuery", .kind = ValueKind::Text, .preview = false, .wrongType = WrongType::Ignore, .block = Block::Pictures},
};

/// @brief Find one row by the identity a shape's switch reads.
constexpr Field const* find(Scope const scope, Slot const slot)
{
    for (Field const& field : kFields)
        if (field.scope == scope and field.slot == slot)
            return &field;
    return nullptr;
}

}
