#pragma once

#include <QRect>
#include <QSize>
#include <QSizeF>

/**
 * @brief Scales @p source_size to fit within @p bounding_size while preserving its aspect ratio.
 *
 * The fitted size touches the bounding box on at least one axis; the other axis is rounded to
 * the nearest pixel and kept at least 1 pixel so very thin sources remain visible.
 *
 * @param source_size Natural size of the content, in any unit.
 * @param bounding_size Available area in pixels.
 * @return The fitted size, or an invalid QSize when @p source_size has no area or
 *         @p bounding_size is invalid.
 */
QSize contained_size_for_aspect(QSizeF source_size, QSize bounding_size);

/**
 * @brief Returns a rectangle that centers @p image_size inside @p bounding_rect.
 *
 * The image is first scaled with contained_size_for_aspect(), so it may be enlarged or reduced.
 *
 * @return The centered rectangle in the coordinate system of @p bounding_rect, or a null
 *         rectangle when @p image_size has no area or @p bounding_rect is invalid.
 */
QRect centered_rect_for_image(QSize image_size, QRect bounding_rect);
