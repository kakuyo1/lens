.pragma library

/// @file Numbers.js
/// @brief Pure number helpers shared by the QML surfaces.
///
/// A library file, not a QML type: nothing here may read surface state, which is what makes the
/// same answer true wherever it is called.

/// @brief Hold a value inside a closed range.
/// @param value The value to hold.
/// @param low The range's low end; the answer when the range is empty.
/// @param high The range's high end.
/// @return @p value, or the nearest end of [low, high]. An empty or reversed range collapses to @p low.
function clamp(value, low, high) {
    return Math.max(low, Math.min(value, Math.max(low, high)));
}
