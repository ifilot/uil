#pragma once

#include "PdfBackend.hpp"

#include <QPdfDocument>

class QtPdfBackend final : public PdfBackend {
public:
    QtPdfBackend();
    ~QtPdfBackend() override;

    bool open(const QString& path, QString* errorMessage = nullptr) override;
    int pageCount() const override;
    QSizeF pageSizePoints(int pageIndex) const override;
    QImage renderPage(int pageIndex, QSize targetPixelSize) override;

private:
    static QString errorToString(QPdfDocument::Error error);

    QPdfDocument m_document;
};
