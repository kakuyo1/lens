#pragma once

/**
 * @file glass_capability.h
 * @brief Which screens the machine has, and which adapter Windows lists each one under.
 *
 * The glass reads the desktop, so what a machine can give it is a per-screen fact, not a per-machine one: on the
 * hybrid laptop of docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md the external monitor
 * duplicates at 137 frames a second while the internal panel refuses duplication on the adapter that owns it. The
 * inventory is logged the first time the glass starts refreshing.
 *
 * Enumeration only. No D3D11 device is created here: a device pulls a driver thread pool and tens of megabytes
 * (docs/metrics/resource-footprint/glass-memory-2026-10-18.md 4.1 and 9.2), and whether a given output can be
 * duplicated is checked by the probe target `lens_gtest_glass_probe`, not by the shipped application.
 */

#include <QStringList>

namespace lens::app {

/**
 * @brief One line per output Windows lists, naming the adapter it is listed under.
 * @return The lines, in enumeration order; empty off Windows and when DXGI refuses to answer.
 * @note Carries hardware layout only: device names, pixel geometry, refresh rate and rotation. Never pixels, never
 *       what is on the screen (SECURITY.md, docs/adr/0034).
 */
QStringList glassScreenLines();

} // namespace lens::app
