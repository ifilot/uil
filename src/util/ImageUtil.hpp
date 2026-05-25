#pragma once

#include <QRect>
#include <QSize>
#include <QSizeF>

QSize containedSizeForAspect(QSizeF sourceSize, QSize boundingSize);
QRect centeredRectForImage(QSize imageSize, QRect boundingRect);
