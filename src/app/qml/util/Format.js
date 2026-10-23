.pragma library

/// @file Format.js
/// @brief Display formatting shared by the QML surfaces.
///
/// A library file, not a QML type: it formats values and knows nothing about the surface that
/// shows them. Anything that needs a translated sentence stays with the surface; translation is
/// context, not formatting.

/// @brief One byte count as the reader sees it: binary units, a decimal below ten.
/// @param bytes The count to write.
/// @return The count with its unit; zero and anything that is not positive read as "0 B".
function formatBytes(bytes) {
    if (!(bytes > 0))
        return "0 B";
    if (bytes < 1024)
        return bytes + " B";
    const units = ["KB", "MB", "GB", "TB"];
    let value = bytes / 1024;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        ++unit;
    }
    return (value < 10 ? value.toFixed(1) : Math.round(value)) + " " + units[unit];
}

/// @brief A currency and an amount as the cost surfaces write them.
/// @param currency The symbol or code the cost duty reports.
/// @param amount The amount in that currency.
/// @return The two joined with two decimals and no space, the shape every cost row uses.
function formatMoney(currency, amount) {
    return currency + amount.toFixed(2);
}
