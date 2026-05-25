#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QString>

class PdfBackend {
public:
    virtual ~PdfBackend() = default;

    virtual bool open(const QString& path, QString* errorMessage = nullptr) = 0;
    virtual int pageCount() const = 0;
    virtual QSizeF pageSizePoints(int pageIndex) const = 0;

    virtual QImage renderPage(int pageIndex, QSize targetPixelSize) = 0;
};
