#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QSize>

namespace FontAwesome {
enum class Style {
    Solid,
    Regular,
    Brands
};

QString resourcePath(Style style, const QString& name);
QIcon icon(Style style, const QString& name, QColor color = QColor(0xcc, 0xcc, 0xcc), QSize size = QSize(32, 32));
}
