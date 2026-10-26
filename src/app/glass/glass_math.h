#pragma once

/**
 * @file glass_math.h
 * @brief The arithmetic the glass themes need: which text tone a backdrop calls for, and the
 *        displacement map the liquid theme bends its backdrop with.
 */

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace lens::app {

/// How much better the other tone has to read before a tone in force gives way. A live sheet is re-read
/// about fifteen times a second (docs/adr/0041), and where the two tones read alike — a mid-grey desktop,
/// which the liquid theme's number reaches at channel value 116 — a sample that shifts by one 8-bit level
/// would otherwise flip the text. This margin leaves greys 112 to 120 undecided, where the settled tone
/// stands; the hold below covers a sample that swings further than that.
constexpr double kToneMargin = 1.15;
/// Reads in a row that must ask for the other tone before a settled one gives way, so a sample swinging
/// between two states (a video behind the card) cannot flip the text on alternate reads.
constexpr int kToneHold = 2;

/**
 * @brief The contrast the two text tones get over a backdrop, once the card's fill is laid on it.
 *
 * Both tones are judged on the 10th percentile of the contrast over the sampled pixels rather than on
 * the average: a card whose text is unreadable in a tenth of its area is not readable, whatever the mean
 * says. The sample is every fourth pixel in each direction, which is plenty for a card of this size.
 */
struct ToneRead {
    double againstDark  = 0.0; ///< 10th-percentile contrast dark text gets over the composite.
    double againstLight = 0.0; ///< The same for light text.

    /// @return Whether light text reads better, the rule with no history (docs/adr/0035). A sheet taken
    ///         afresh is judged by this, once; the ~15/s refresh goes through settleTone().
    bool light() const noexcept
    {
        return againstLight > againstDark;
    }
};

/**
 * @brief Read the tone a backdrop calls for: the contrast each tone gets over the desktop under the card.
 *
 * @param backdrop The desktop picture under the card. Read here and not kept.
 * @param fillAlpha Opacity of the white fill over the picture, 0 to 1: 0.4 for the frosted theme, 0 for liquid.
 * @return The two tones' 10th-percentile contrast over the composite. Both zero for an empty backdrop.
 */
ToneRead readTone(QImage const& backdrop, double fillAlpha);

/**
 * @brief Read the tone over one rectangle of a backdrop, without copying it out first.
 *
 * The live refresh reads the tone over the card on every published frame, and the card is a rectangle of the sheet.
 * Copying that rectangle out of a native-resolution sheet only to sample every fourth pixel of it is one of the
 * per-frame costs that kept the duplicated path at sixty a second (docs/adr/0074).
 *
 * @param backdrop The desktop picture under the card. Read here and not kept.
 * @param region The card's rectangle, in the picture's pixels; clamped to the picture.
 * @param fillAlpha Opacity of the white fill over the picture, 0 to 1.
 * @return The two tones' 10th-percentile contrast over @p region; both zero when it is empty.
 */
ToneRead readTone(QImage const& backdrop, QRect const& region, double fillAlpha);

/**
 * @brief The tone to settle on, from a fresh read and the tone already in force.
 *
 * A tone in force is sticky: the other one has to win by kToneMargin, and to do so on @p hold reads
 * running. Without that a card sitting where the two tones read alike would re-decide its text with
 * every refresh, and the reader would see it flicker.
 *
 * @param read The backdrop's read.
 * @param settledLight The tone in force.
 * @param streak Reads in a row that have asked for the other tone; counted here. Reset it with a new sheet.
 * @param hold How many such reads the other tone needs: 1 where the sheet was just taken and this read is
 *             the only judgement there is, kToneHold for the refresh of a settled sheet.
 * @return The tone to show.
 */
bool settleTone(ToneRead const& read, bool settledLight, int& streak, int hold);

/**
 * @brief The displacement map the liquid theme samples its backdrop with.
 *
 * The offset is taken from the gradient of a blurred rounded-rectangle mask, read from its alpha channel:
 * the mask is steepest at the edge and flat toward the centre, so the centre is bent too without a seam
 * along the diagonal. The red and green channels hold the offset in [0, 255], 128 meaning none; the
 * shader turns it into a sample offset with the refraction distance.
 *
 * @param size The card the map covers, in device-independent pixels.
 * @param radius Corner radius of the card.
 * @param blurPx Softness of the mask the gradient is taken from.
 * @return An ARGB32 image of @p size, or a null image when @p size is empty.
 */
QImage displacementMap(QSize size, double radius, double blurPx);

/**
 * @brief The displacement map the liquid glass bends its picture with, for a card of @p size.
 *
 * It depends on the card's size and corners and on neither the blur nor the refraction, so its owner keeps it
 * between requests rather than rebuilding it for every slider step.
 *
 * @param size The card's size, in device pixels.
 * @param radius Corner radius of the card, in device-independent pixels.
 * @param scale The device pixel ratio the card is drawn at.
 * @return An ARGB32 image of @p size, as displacementMap() returns it.
 */
QImage refractionMap(QSize size, double radius, double scale);

/**
 * @brief A rectangle on the desktop in device pixels, from one in device-independent pixels on a screen.
 *
 * The rectangle is measured from the screen's own corner and scaled by that screen's ratio, so Qt's rounding of
 * the screen's place in the logical desktop does not come into it: only the device pixel the screen starts at does.
 *
 * @param logical The rectangle, in device-independent pixels, lying on the screen.
 * @param screenLogical The screen's geometry, in device-independent pixels.
 * @param nativeOrigin The first device pixel of the screen on the desktop.
 * @param scale Device pixels per device-independent pixel on that screen.
 * @return The rectangle in device pixels on the desktop.
 */
QRect toNativeRect(QRect logical, QRect screenLogical, QPoint nativeOrigin, double scale);

/// Side of the grey thumbnail a picture's fingerprint is taken from: see pictureFingerprint().
constexpr int kFingerprintSide = 32;

/**
 * @brief A value that changes when the desktop behind a card changes, and not when it does not.
 *
 * A live sheet is retaken about fifteen times a second (docs/adr/0041). On a still desktop -- the reader is reading,
 * not scrolling -- every retake produces the same picture, and publishing it again costs a blur and three plane
 * allocations for a card that would look identical.
 *
 * The picture is reduced to a 32 by 32 grey thumbnail before hashing, so a caret blinking or a pixel changing
 * somewhere under the card does not count: the blur would have hidden it anyway.
 *
 * @param picture The desktop under the card, as taken.
 * @return A hash of the thumbnail; 0 for a null picture.
 */
quint64 pictureFingerprint(QImage const& picture);

} // namespace lens::app
