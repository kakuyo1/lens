#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/review_schedule.h"

/**
 * @file review_store.h
 * @brief The review curve per word and the check frequency, kept in the settings document.
 *
 * This is a section of the settings document, like StatsStore: it binds to the document
 * KnownStore owns and never reads or writes the file. Keys live under one `review` object:
 * `frequency` (an int, a ReviewFrequency) and `words` (lemma -> {level, due, asked}).
 *
 * It keeps no typed copy. The document is the state, so keys this version does not know are
 * left where they are, and a malformed word entry reads as absent until the reader answers it.
 */

namespace lens::core {

/// @brief Review state per word, and the reader's check frequency, bound to the settings document.
class ReviewStore {
public:
    /**
     * @param document The document KnownStore owns, which must outlive this object.
     */
    explicit ReviewStore(nlohmann::json& document);

    /// @return The stored state for a lemma; std::nullopt when it has none or the entry is malformed.
    std::optional<ReviewState> get(std::string const& lemma) const;

    /// @brief Store a lemma's state, replacing any previous one.
    void put(std::string const& lemma, ReviewState state);

    /// @return True when a state was removed.
    bool removeLemma(std::string const& lemma);

    /// @return The reader's check frequency; the default when unset or out of range.
    ReviewFrequency frequency() const;

    /// @brief Set the check frequency. Takes effect at the next check; no curve moves.
    void setFrequency(ReviewFrequency frequency);

private:
    nlohmann::json& doc_;
};

}
