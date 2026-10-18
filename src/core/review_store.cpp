/**
 * @file review_store.cpp
 * @brief Reads and writes the `review` object of the settings document, for review_store.h.
 */

#include "core/review_store.h"

namespace lens::core {
namespace {

/// @brief The `review` object, or nullptr when the document has none.
nlohmann::json const* reviewOf(nlohmann::json const& document)
{
    auto const review = document.find("review");
    return review != document.end() and review->is_object() ? &*review : nullptr;
}

/// @brief The `review.words` object, or nullptr when there is none.
nlohmann::json const* wordsOf(nlohmann::json const& document)
{
    auto const* review = reviewOf(document);
    if (review == nullptr)
        return nullptr;
    auto const words = review->find("words");
    return words != review->end() and words->is_object() ? &*words : nullptr;
}

/// @brief The object under @p key, replaced by an empty one when absent or of another type.
nlohmann::json& objectAt(nlohmann::json& parent, std::string const& key)
{
    auto& child = parent[key];
    if (not child.is_object())
        child = nlohmann::json::object();
    return child;
}

/// @return One word entry as a state; std::nullopt unless all three fields are integers and the level is in range.
std::optional<ReviewState> stateFromJson(nlohmann::json const& entry)
{
    if (not entry.is_object())
        return std::nullopt;
    auto const field = [&entry](char const* key) -> std::optional<long long> {
        auto const found = entry.find(key);
        if (found == entry.end() or not found->is_number_integer())
            return std::nullopt;
        return found->get<long long>();
    };
    auto const level = field("level");
    auto const due   = field("due");
    auto const asked = field("asked");
    if (not level or not due or not asked or *level < 0 or *level > kMaxReviewLevel)
        return std::nullopt;
    return ReviewState{static_cast<int>(*level), *due, *asked};
}

}

ReviewStore::ReviewStore(nlohmann::json& document)
    : doc_(document)
{
}

std::optional<ReviewState> ReviewStore::get(std::string const& lemma) const
{
    auto const* words = wordsOf(doc_);
    if (words == nullptr or not words->contains(lemma))
        return std::nullopt;
    return stateFromJson(words->at(lemma));
}

void ReviewStore::put(std::string const& lemma, ReviewState const state)
{
    objectAt(objectAt(doc_, "review"), "words")[lemma] = {{"level", state.level}, {"due", state.due}, {"asked", state.asked}};
}

bool ReviewStore::removeLemma(std::string const& lemma)
{
    auto const* words = wordsOf(doc_);
    if (words == nullptr or not words->contains(lemma))
        return false;
    doc_["review"]["words"].erase(lemma);
    return true;
}

ReviewFrequency ReviewStore::frequency() const
{
    auto const* review = reviewOf(doc_);
    if (review == nullptr)
        return kDefaultReviewFrequency;
    auto const value = review->find("frequency");
    if (value == review->end() or not value->is_number_integer())
        return kDefaultReviewFrequency;
    auto const raw = value->get<int>();
    if (raw < static_cast<int>(ReviewFrequency::Off) or raw > static_cast<int>(kLastReviewFrequency))
        return kDefaultReviewFrequency;
    return static_cast<ReviewFrequency>(raw);
}

void ReviewStore::setFrequency(ReviewFrequency const frequency)
{
    objectAt(doc_, "review")["frequency"] = static_cast<int>(frequency);
}

}
