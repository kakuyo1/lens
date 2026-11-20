#pragma once

/**
 * @file settings_rows.h
 * @brief Reading a settings-table row out of a section, for core readers that have no QVariant.
 *
 * app/settings_access.h spells the rule per kind in full; this header carries the one shape core
 * reads, taken from the row rather than restated, so a missing key answers the row's own default
 * and a reader cannot bypass it by naming the key alone (docs/adr/0083).
 */

#include "core/domain/settings_table.h"
#include "settings_section.h"

namespace lens::core {

inline bool readToggle(SettingsSection const& section, settings::Row const& row)
{
    return section.value<bool>(row.key, row.fallback.toggle);
}

} // namespace lens::core
