#pragma once

/**
 * @file taskbar.h
 * @brief The Windows taskbar's own state that the screen rectangles cannot tell: whether it hides itself.
 *
 * The taskbar's strip is read from QScreen's whole and usable rectangles (PetTaskbar::stripOf); only the auto-hide
 * preference needs the shell. Windows only, like the rest of src/app.
 */

namespace lens::app {

/**
 * @brief Whether the reader has set the taskbar to hide itself.
 *
 * The shell's appbar state is the one authority: a work area that still excludes an auto-hidden taskbar is not
 * distinguishable from a fixed one by rectangles alone.
 * @return True when the taskbar is set to auto-hide. The appbar call has no separate failure value; zero is the
 *         ordinary "visible, always on top" answer.
 */
bool taskbarAutoHides();

} // namespace lens::app
