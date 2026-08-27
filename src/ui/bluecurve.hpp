#pragma once

#include <QIcon>
#include <QString>
#include <QSize>

namespace bluecurve {
/** @brief Returns the bundled Bluecurve action-icon resource path. */
QString resource_path(const QString& name, int nominal_size);

/** @brief Renders a bundled Bluecurve action icon at the requested size. */
QIcon icon(const QString& name, QSize size = QSize(16, 16));
}
