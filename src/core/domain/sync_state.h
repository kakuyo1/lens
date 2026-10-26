#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "core/document/known_store.h"
#include "core/document/settings_section.h"
#include "core/document/stats_store.h"

/**
 * @file sync_state.h
 * @brief The Qt-free part of one sync: what this device sends, and how a merged document is written
 *        back into the stores (docs/adr/0042).
 *
 * Nothing here touches the network or the file. SyncRound runs the requests and calls these in
 * order: build the local document, merge it with the cloud's, apply the result, then seal it. Keeping
 * the mapping pure is what lets the tests assert each step against a store they built in memory.
 */

namespace lens::core::sync {

/// @return The whitelisted settings keys, in the order a document lists them. `level` and
///         `explanationLang` are among them, but KnownStore holds those, not the document.
const std::vector<std::string>& syncedKeys();

/**
 * @brief This device's side of a sync: the document merge() reads as `local`.
 *
 * @param store      Marks, level and explanation language.
 * @param stats      History and this device's own daily tallies.
 * @param snapshot   The settings object as the last successful sync left it: key -> {value, at}.
 * @param deviceId   This device's id, the key of its own daily bucket.
 * @param nowMs      The current time in ms since the epoch; stamps a setting that changed since the snapshot.
 * @return The document, with `format`, `known`, `deleted`, `history`, `daily` and `settings`.
 */
nlohmann::json buildLocalDocument(const KnownStore&     store,
                                  const StatsStore&     stats,
                                  const nlohmann::json& snapshot,
                                  const std::string&    deviceId,
                                  long long             nowMs);

/**
 * @brief What a sync would send, as one string. Equal strings mean nothing a sync carries has changed.
 *
 * @note Stamps are fixed at zero, so the string depends on the values only, not on when they changed,
 *       and a change to a key the cloud copy does not carry (an API key, a hotkey) leaves it as it was.
 */
std::string contentFingerprint(const KnownStore& store, const StatsStore& stats);

/// @return The whitelisted entries of a merged `settings` object, which is what the next sync diffs against.
nlohmann::json snapshotOf(const nlohmann::json& settings);

/// @brief Write the merged marks into the store. The caller saves.
void applyMarks(KnownStore& store, const nlohmann::json& merged);

/// @brief Replace the history with the merged rows. Rows that are not objects with a word and a minute are skipped.
void applyHistory(StatsStore& stats, const nlohmann::json& merged);

/// @brief Keep the other devices' daily tallies from the merged document; this device's bucket is left alone.
void applyDaily(StatsStore& stats, const nlohmann::json& merged, const std::string& deviceId);

/**
 * @brief Write the merged settings that differ from this device's. Level and language go through the
 *        store, the rest through the section; each value keeps the form the merge gave it.
 * @note A level outside 0..7 or an empty language is refused rather than written: the cloud copy is
 *       ours, but a corrupted one should not be able to break the store.
 */
void applySettings(KnownStore& store, SettingsSection section, const nlohmann::json& merged);

}
