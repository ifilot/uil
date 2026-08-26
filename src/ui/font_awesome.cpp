#include "ui/font_awesome.hpp"

#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {
/** @brief Returns the process-local cache of rasterized interface icons. */
QHash<QString, QIcon>& icon_cache() {
    static QHash<QString, QIcon> cache;
    return cache;
}

/** @brief Maps an icon family to its bundled resource directory. */
QString style_directory(font_awesome::Style style) {
    switch (style) {
    case font_awesome::Style::Solid:
        return QStringLiteral("solid");
    case font_awesome::Style::Regular:
        return QStringLiteral("regular");
    case font_awesome::Style::Brands:
        return QStringLiteral("brands");
    }

    return QStringLiteral("solid");
}
}

QString font_awesome::resource_path(Style style, const QString& name) {
    return QStringLiteral(":/fontawesome/svgs/%1/%2.svg").arg(style_directory(style), name);
}

QIcon font_awesome::icon(Style style, const QString& name, QColor color, QSize size) {
    const QSize icon_size = size.isValid() ? size : QSize(32, 32);
    const QString cache_key = QStringLiteral("%1|%2|%3|%4x%5")
                                  .arg(int(style))
                                  .arg(name, color.name(QColor::HexArgb))
                                  .arg(icon_size.width())
                                  .arg(icon_size.height());
    if (const auto cached = icon_cache().constFind(cache_key); cached != icon_cache().cend()) {
        return *cached;
    }

    QFile svgFile(resource_path(style, name));
    if (!svgFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QByteArray svgData = svgFile.readAll();
    svgData.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QSize renderedSize = renderer.defaultSize().scaled(icon_size, Qt::KeepAspectRatio);
    if (!renderedSize.isValid()) {
        renderedSize = icon_size;
    }
    const QRectF target(
        (icon_size.width() - renderedSize.width()) / 2.0,
        (icon_size.height() - renderedSize.height()) / 2.0,
        renderedSize.width(),
        renderedSize.height());
    renderer.render(&painter, target);
    const QIcon rendered_icon(pixmap);
    icon_cache().insert(cache_key, rendered_icon);
    return rendered_icon;
}
