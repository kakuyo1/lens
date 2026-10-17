#pragma once

#include <QString>
#include <QVariantList>

#include <string_view>

#include "core/known_store.h"
#include "core/review_store.h"
#include "core/settings_section.h"
#include "core/stats_store.h"

/**
 * @file storage_duty.h
 * @brief The single-document storage boundary used by the application duties.
 */

namespace lens::app {

class StorageDuty final {
public:
    /**
     * @brief Bind the storage boundary to the document owned by @p store.
     * @param store The one owner of the reader's settings document.
     */
    explicit StorageDuty(core::KnownStore& store);

    /// @return The document owner shared by the other duties.
    core::KnownStore&       knownStore();
    const core::KnownStore& knownStore() const;

    /// @return The statistics section bound to the same document.
    core::StatsStore&       statsStore();
    const core::StatsStore& statsStore() const;

    /// @return The review section bound to the same document.
    core::ReviewStore&       reviewStore();
    const core::ReviewStore& reviewStore() const;

    /**
     * @brief The document, or one of its nested objects, as the unit a key is read and written through.
     * @param name Object name to bind to; empty for the document itself.
     * @note Every write through the section saves. Nine keys are re-stamped from their owner's own
     *       state and are not the section's to write; docs/adr/0024 names them.
     */
    core::SettingsSection section(std::string_view name = {});

    /// @brief Persist the current document, including statistics changes.
    void save() const;

    /// @brief Remove a lemma from marks, explanation cache, history, and review state in one save.
    /// @return True when any local word state existed.
    bool removeWord(QString const& lemma);

    /// @return The vocabulary level options shown by the settings surface.
    static QVariantList levelOptions();

    /// @return The word-review check frequencies the settings surface offers, as {value, label}; the values are core::ReviewFrequency.
    static QVariantList reviewFrequencyOptions();

private:
    core::KnownStore& store_;
    core::StatsStore  stats_;
    core::ReviewStore review_;
};

}
