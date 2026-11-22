#include "image_util.hpp"

#include <QtMath>

QSize contained_size_for_aspect(QSizeF source_size, QSize bounding_size) {
    if (source_size.isEmpty() || !bounding_size.isValid()) {
        return {};
    }

    const double source_aspect = source_size.width() / source_size.height();
    const double bounding_aspect = double(bounding_size.width()) / double(bounding_size.height());

    QSize result;
    if (source_aspect > bounding_aspect) {
        result.setWidth(bounding_size.width());
        result.setHeight(qMax(1, int(qRound(double(bounding_size.width()) / source_aspect))));
    } else {
        result.setHeight(bounding_size.height());
        result.setWidth(qMax(1, int(qRound(double(bounding_size.height()) * source_aspect))));
    }

    return result;
}

QRect centered_rect_for_image(QSize image_size, QRect bounding_rect) {
    if (image_size.isEmpty() || !bounding_rect.isValid()) {
        return {};
    }

    const QSize fitted = contained_size_for_aspect(image_size, bounding_rect.size());
    const int x = bounding_rect.x() + (bounding_rect.width() - fitted.width()) / 2;
    const int y = bounding_rect.y() + (bounding_rect.height() - fitted.height()) / 2;
    return QRect(QPoint(x, y), fitted);
}
