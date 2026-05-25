#include "ImageUtil.hpp"

#include <QtMath>

QSize containedSizeForAspect(QSizeF sourceSize, QSize boundingSize) {
    if (!sourceSize.isValid() || !boundingSize.isValid()) {
        return {};
    }

    const double sourceAspect = sourceSize.width() / sourceSize.height();
    const double boundingAspect = double(boundingSize.width()) / double(boundingSize.height());

    QSize result;
    if (sourceAspect > boundingAspect) {
        result.setWidth(boundingSize.width());
        result.setHeight(qMax(1, int(qRound(double(boundingSize.width()) / sourceAspect))));
    } else {
        result.setHeight(boundingSize.height());
        result.setWidth(qMax(1, int(qRound(double(boundingSize.height()) * sourceAspect))));
    }

    return result;
}

QRect centeredRectForImage(QSize imageSize, QRect boundingRect) {
    if (!imageSize.isValid() || !boundingRect.isValid()) {
        return {};
    }

    const QSize fitted = containedSizeForAspect(imageSize, boundingRect.size());
    const int x = boundingRect.x() + (boundingRect.width() - fitted.width()) / 2;
    const int y = boundingRect.y() + (boundingRect.height() - fitted.height()) / 2;
    return QRect(QPoint(x, y), fitted);
}
