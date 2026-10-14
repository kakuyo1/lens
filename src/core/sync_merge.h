#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

/**
 * @file sync_merge.h
 * @brief The merge a sync runs between this device's settings document and the copy in the cloud.
 *
 * Pure JSON in, JSON out: no file, no network, no Qt. The rules are docs/adr/0042's, and
 * sync_merge.cpp states each one where it is applied. The result is the document both sides keep:
 * the local side writes it back, and the encrypted form of it is what goes up.
 */

namespace lens::core::sync {

/// @brief The document format this merge reads and writes.
inline constexpr int kFormat = 1;

/// @brief Rows of query history kept after a merge; the oldest beyond this are dropped.
/// @note Matches StatsStore::kMaxHistory, so a merged history never holds more than a local one.
inline constexpr std::size_t kMaxHistory = 2000;

/**
 * @brief Merge this device's document with the cloud's.
 *
 * @param local       This device's document. Its `daily` bucket for @p localDevice is the one kept.
 * @param remote      The decrypted cloud document.
 * @param localDevice This device's id, the key of its own `daily` bucket.
 * @return A document with `format`, `known`, `deleted`, `history`, `daily` and `settings`. Each part
 *         holds only the winners. A missing or malformed field reads as empty, and nothing throws.
 * @note Merging the result again with the same inputs gives the same result.
 */
nlohmann::json merge(const nlohmann::json& local, const nlohmann::json& remote, const std::string& localDevice);

} // namespace lens::core::sync
