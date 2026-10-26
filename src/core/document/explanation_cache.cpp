/**
 * @file explanation_cache.cpp
 * @brief The context, the tri-state and the store hand-off for the explanation cache.
 */

#include "core/document/explanation_cache.h"

#include "core/document/settings_rows.h"

namespace lens::core {

ExplanationCache::ExplanationCache(KnownStore& store)
    : store_(store)
{
}

CacheContext ExplanationCache::readerContext() const
{
    return {store_.explanationLang(), readToggle(store_.section(), *settings::find("multiSense"))};
}

CacheContext ExplanationCache::contextFor(SelectionKind const kind) const
{
    return masked(readerContext(), kind);
}

CacheContext ExplanationCache::masked(CacheContext const& captured, SelectionKind const kind)
{
    return {captured.language, captured.multipleSenses and kind == SelectionKind::Word};
}

ExplanationCache::Look ExplanationCache::look(std::string const& lemma, CacheContext const& context) const
{
    auto const cached = store_.cacheGet(lemma, context);
    if (not cached)
        return {};
    // docs/adr/0033: an entry bought without asking for an example is not an answer while the
    // setting is on -- it has to be bought again. An empty example is a settled answer, so it hits.
    if (readToggle(store_.section(), *settings::find("example")) and not cached->example)
        return {State::Rebuy, std::move(*cached)};
    return {State::Hit, std::move(*cached)};
}

void ExplanationCache::store(std::string const& lemma, WordCache entry, CacheContext const& context)
{
    store_.cachePut(lemma, std::move(entry), context);
}

}
