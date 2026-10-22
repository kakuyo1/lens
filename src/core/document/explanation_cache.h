#pragma once

#include <string>

#include "core/domain/filter_core.h"
#include "core/document/known_store.h"

/**
 * @file explanation_cache.h
 * @brief The rules of the explanation cache: the context a request goes out under, and what a
 *        stored entry is worth to the reader now.
 *
 * The context used to be built by hand at every call site that needed one -- nine in
 * ExplanationDuty and one in ReviewDuty -- and the rule "the sense mode counts only on the word
 * channel" was spelled at three of them and re-implemented twice more inside the queue. Here it
 * is spelled once (docs/adr/0085). The key the entries live under stays the store's own
 * (docs/adr/0019), and the tri-state is the one the bubble renders (docs/adr/0033).
 */

namespace lens::core {

/**
 * @brief The explanation cache's rules, over one store's cache.
 *
 * The store owns the entries; this module owns what they mean. The duty asks instead of building
 * structs, and the queue re-uses the mask through masked(). Holds no state of its own beyond the
 * store it reads.
 */
class ExplanationCache {
public:
    /// @brief What the cache holds for a word, in the states the reader's paths distinguish.
    enum class State {
        Missing, ///< Nothing is stored under this context.
        Rebuy,   ///< An entry is stored, but it was bought without an example and the reader's setting now asks for one.
        Hit,     ///< A stored entry the reader can be served whole.
    };
    struct Look {
        State state = State::Missing;
        /// @brief The stored entry; default when Missing, and still carried by a Rebuy -- the hover
        ///        review shows what is stored rather than spending money on it.
        WordCache entry;
    };

    explicit ExplanationCache(KnownStore& store);

    /// @return The language and sense mode as the settings stand, unmasked: the queue's "now",
    ///         against which an answer still in flight is told apart from the settings having moved.
    CacheContext readerContext() const;

    /// @return The context a request for a selection of this kind goes out under: the settings as
    ///         they stand, with the sense mode masked off every channel but the word's.
    CacheContext contextFor(SelectionKind kind) const;

    /// @return The word-channel mask over a context already captured: the one spelling of the rule
    ///         that the sense mode, the explanation cache and the word's origin are the word
    ///         channel's business alone.
    static CacheContext masked(CacheContext const& captured, SelectionKind kind);

    /// @brief What the cache holds for @p lemma under @p context.
    /// @return The state, and the entry when there is one. The example setting is read here, so the
    ///         rule that an entry bought without an example must be bought again while the setting
    ///         is on (docs/adr/0033) is the module's to apply, not every caller's to re-derive.
    Look look(std::string const& lemma, CacheContext const& context) const;

    /// @brief Put a paid-for answer away under the context it was bought in.
    /// @note The entry's example field is the caller's to fill -- from the batch's own flag, not the
    ///       current setting, so a sentence paid for is kept even if the setting was turned off while
    ///       the request was away (docs/adr/0033).
    void store(std::string const& lemma, WordCache entry, CacheContext const& context);

private:
    KnownStore& store_;
};

}
