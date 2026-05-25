#include "QtPdfBackend.hpp"

#include <QLoggingCategory>
#include <QPdfDocumentRenderOptions>

Q_LOGGING_CATEGORY(logPdf, "pdf")

QtPdfBackend::QtPdfBackend() = default;
QtPdfBackend::~QtPdfBackend() = default;

bool QtPdfBackend::open(const QString& path, QString* errorMessage) {
    const QPdfDocument::Error error = m_document.load(path);
    if (error != QPdfDocument::Error::None) {
        if (errorMessage) {
            *errorMessage = errorToString(error);
        }
        qCWarning(logPdf) << "Failed to open PDF" << path << errorToString(error);
        return false;
    }

    qCInfo(logPdf) << "Opened PDF" << path << "pages:" << m_document.pageCount();
    return true;
}

int QtPdfBackend::pageCount() const {
    return m_document.pageCount();
}

QSizeF QtPdfBackend::pageSizePoints(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= m_document.pageCount()) {
        return {};
    }
    return m_document.pagePointSize(pageIndex);
}

QImage QtPdfBackend::renderPage(int pageIndex, QSize targetPixelSize) {
    if (pageIndex < 0 || pageIndex >= m_document.pageCount() || !targetPixelSize.isValid()) {
        return {};
    }

    QPdfDocumentRenderOptions options;
    return m_document.render(pageIndex, targetPixelSize, options);
}

QString QtPdfBackend::errorToString(QPdfDocument::Error error) {
    switch (error) {
    case QPdfDocument::Error::None:
        return QStringLiteral("No error");
    case QPdfDocument::Error::Unknown:
        return QStringLiteral("Unknown PDF error");
    case QPdfDocument::Error::DataNotYetAvailable:
        return QStringLiteral("PDF data is not available yet");
    case QPdfDocument::Error::FileNotFound:
        return QStringLiteral("PDF file not found");
    case QPdfDocument::Error::InvalidFileFormat:
        return QStringLiteral("Invalid PDF file format");
    case QPdfDocument::Error::IncorrectPassword:
        return QStringLiteral("PDF requires a password");
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return QStringLiteral("PDF security scheme is unsupported");
    }

    return QStringLiteral("Unexpected PDF error");
}
