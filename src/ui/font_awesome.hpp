#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>

namespace font_awesome {
/** @brief Identifies a bundled Font Awesome icon family. */
enum class Style { Solid, Regular, Brands };

/**
 * @brief Returns the Qt resource path for an icon.
 * @param style Icon family, selecting the `solid`, `regular` or `brands` directory.
 * @param name Font Awesome icon name without the `.svg` extension.
 * @return A path of the form `:/fontawesome/svgs/<family>/<name>.svg`; existence is not checked.
 */
QString resource_path(Style style, const QString& name);

/**
 * @brief Creates a rendered icon with the requested family, color, and size.
 *
 * Every `currentColor` in the SVG is replaced with the opaque RGB value of @p color (its alpha
 * is ignored) and the glyph is rasterized once, centered and aspect-preserving, into a
 * transparent pixmap. Results are cached per process; because the cache and QPixmap are not
 * thread-safe, call this from the GUI thread only.
 *
 * @param size Pixmap size; an invalid size falls back to 32x32.
 * @return The rendered icon, or a null icon when the resource is missing or invalid.
 */
QIcon icon(Style style, const QString& name, QColor color = QColor(0xcc, 0xcc, 0xcc),
           QSize size = QSize(32, 32));
}  // namespace font_awesome
