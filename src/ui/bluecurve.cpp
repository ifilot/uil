#include "ui/bluecurve.hpp"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {
/** @brief Returns the process-local cache of rasterized Bluecurve icons. */
QHash<QString, QIcon>& icon_cache() {
    static QHash<QString, QIcon> cache;
    return cache;
}
}

QString bluecurve::resource_path(const QString& name, int nominal_size) {
    return QStringLiteral(":/bluecurve/%1x%1/actions/%2.svg").arg(nominal_size).arg(name);
}

QIcon bluecurve::icon(const QString& name, QSize size) {
    const QSize icon_size = size.isValid() ? size : QSize(16, 16);
    const int nominal_size = qMax(icon_size.width(), icon_size.height()) <= 16 ? 16 : 20;
    const QString cache_key = QStringLiteral("%1|%2x%3")
                                  .arg(name)
                                  .arg(icon_size.width())
                                  .arg(icon_size.height());
    if (const auto cached = icon_cache().constFind(cache_key); cached != icon_cache().cend()) {
        return *cached;
    }

    QSvgRenderer renderer(resource_path(name, nominal_size));
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(icon_size)));

    const QIcon rendered_icon(pixmap);
    icon_cache().insert(cache_key, rendered_icon);
    return rendered_icon;
}
