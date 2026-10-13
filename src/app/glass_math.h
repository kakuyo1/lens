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

/**
 * @brief Whether light text reads better than dark text over this backdrop, once the card's fill is laid on it.
 *
 * Both tones are judged on the 10th percentile of the contrast over the sampled pixels rather than on
 * the average: a card whose text is unreadable in a tenth of its area is not readable, whatever the mean
 * says. The sample is every fourth pixel in each direction, which is plenty for a card of this size.
 *
 * @param backdrop The desktop picture under the card. Read here and not kept.
 * @param fillAlpha Opacity of the white fill over the picture, 0 to 1: 0.4 for the frosted theme, 0 for liquid.
 * @return True when light text's 10th-percentile contrast is the higher one.
 */
bool prefersLightText(QImage const& backdrop, double fillAlpha);

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
 * @brief The desktop picture blurred and saturated: the sheet the glass is cut from (docs/adr/0039).
 *
 * The blur follows CSS blur(): a Gaussian whose deviation is the percentage of 60 px, at 1x. Three box passes
 * approximate it. The saturation is CSS saturate(), so 0 is grey and 100 is untouched. Corners and refraction are
 * not applied here: the shader places the card within this picture on every frame.
 *
 * @param picture The desktop under the sheet, in device pixels. Its devicePixelRatio scales the blur.
 * @param blurPercent Blur, 0 to 100.
 * @param saturationPercent Saturation, 0 to 200; 100 is untouched.
 * @return An opaque RGB32 image of the picture's size.
 */
QImage blurredPicture(QImage const& picture, double blurPercent, double saturationPercent);

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

} // namespace lens::app
