#include "ui/FontAwesome.hpp"

#include <QFile>
#include <QIODevice>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {
QString styleDirectory(FontAwesome::Style style) {
    switch (style) {
    case FontAwesome::Style::Solid:
        return QStringLiteral("solid");
    case FontAwesome::Style::Regular:
        return QStringLiteral("regular");
    case FontAwesome::Style::Brands:
        return QStringLiteral("brands");
    }

    return QStringLiteral("solid");
}
}

QString FontAwesome::resourcePath(Style style, const QString& name) {
    return QStringLiteral(":/fontawesome/svgs/%1/%2.svg").arg(styleDirectory(style), name);
}

QIcon FontAwesome::icon(Style style, const QString& name, QColor color, QSize size) {
    QFile svgFile(resourcePath(style, name));
    if (!svgFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QByteArray svgData = svgFile.readAll();
    svgData.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return {};
    }

    const QSize iconSize = size.isValid() ? size : QSize(32, 32);
    QPixmap pixmap(iconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QSize renderedSize = renderer.defaultSize().scaled(iconSize, Qt::KeepAspectRatio);
    if (!renderedSize.isValid()) {
        renderedSize = iconSize;
    }
    const QRectF target(
        (iconSize.width() - renderedSize.width()) / 2.0,
        (iconSize.height() - renderedSize.height()) / 2.0,
        renderedSize.width(),
        renderedSize.height());
    renderer.render(&painter, target);
    return QIcon(pixmap);
}
