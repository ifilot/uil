#pragma once

#include <QImage>
#include <QString>

QImage extractFirstVideoFrame(const QString& path, QString* errorMessage = nullptr);
