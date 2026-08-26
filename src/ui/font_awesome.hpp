#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QSize>

namespace font_awesome {
/** @brief Identifies a bundled Font Awesome icon family. */
enum class Style {
    Solid,
    Regular,
    Brands
};

/** @brief Returns the Qt resource path for an icon. */
QString resource_path(Style style, const QString& name);

/** @brief Creates a rendered icon with the requested family, color, and size. */
QIcon icon(Style style, const QString& name, QColor color = QColor(0xcc, 0xcc, 0xcc), QSize size = QSize(32, 32));
}
