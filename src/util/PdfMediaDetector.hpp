#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

struct PdfMediaAnnotation {
    int pageIndex = -1;
    int objectNumber = -1;
    QString subtype;
    QString fileName;
    QString resolvedFilePath;
    QRectF rect;
    QImage firstFrame;

    bool hasFirstFrame() const;
    bool isMp4() const;
};

struct PdfMediaScanResult {
    QVector<PdfMediaAnnotation> annotations;

    bool hasMedia() const;
    QString summary() const;
};

PdfMediaScanResult scanPdfMediaAnnotations(
    const QString& path,
    const QString& packageRootPath = QString(),
    const QStringList& packageMovieAssetPaths = {});
