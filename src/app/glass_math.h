#pragma once

/**
 * @file glass_math.h
 * @brief The arithmetic the glass themes need: which text tone a backdrop calls for, and the
 *        displacement map the liquid theme bends its backdrop with.
 */

#include <QImage>
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

} // namespace lens::app
