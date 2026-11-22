#pragma once

#include <QIcon>
#include <QSize>
#include <QString>

/** @brief Bundled Bluecurve action icons rendered from SVG resources. */
namespace bluecurve {
/**
 * @brief Returns the bundled Bluecurve action-icon resource path.
 * @param name Icon name without the `.svg` extension.
 * @param nominal_size Source icon set, such as 16 or 20 (pixels).
 * @return A path of the form `:/bluecurve/<n>x<n>/actions/<name>.svg`; existence is not checked.
 */
QString resource_path(const QString& name, int nominal_size);

/**
 * @brief Renders a bundled Bluecurve action icon at the requested size.
 *
 * Uses the 16x16 source set when both dimensions are at most 16 pixels and the 20x20 set
 * otherwise. Results are cached per process; because the cache and QPixmap are not
 * thread-safe, call this from the GUI thread only.
 *
 * @param size Pixmap size; an invalid size falls back to 16x16.
 * @return The rendered icon, or a null icon when the resource is missing or invalid.
 */
QIcon icon(const QString& name, QSize size = QSize(16, 16));
}  // namespace bluecurve
