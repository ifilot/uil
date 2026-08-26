#pragma once

#include <QRect>
#include <QSize>
#include <QSizeF>

/** @brief Scales @p sourceSize to fit within @p boundingSize while preserving its aspect ratio. */
QSize contained_size_for_aspect(QSizeF sourceSize, QSize boundingSize);

/** @brief Returns a rectangle that centers @p imageSize inside @p boundingRect. */
QRect centered_rect_for_image(QSize imageSize, QRect boundingRect);
