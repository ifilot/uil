#include "qt_pdf_backend.hpp"

#include <QLoggingCategory>
#include <QPdfDocumentRenderOptions>

Q_LOGGING_CATEGORY(logPdf, "pdf")

QtPdfBackend::QtPdfBackend() = default;
QtPdfBackend::~QtPdfBackend() = default;

bool QtPdfBackend::open(const QString& path, QString* error_message) {
    const QPdfDocument::Error error = document_.load(path);
    if (error != QPdfDocument::Error::None) {
        if (error_message) {
            *error_message = error_to_string(error);
        }
        qCWarning(logPdf) << "Failed to open PDF" << path << error_to_string(error);
        return false;
    }

    qCInfo(logPdf) << "Opened PDF" << path << "pages:" << document_.pageCount();
    return true;
}

int QtPdfBackend::page_count() const {
    return document_.pageCount();
}

QSizeF QtPdfBackend::page_size_points(int page_index) const {
    if (page_index < 0 || page_index >= document_.pageCount()) {
        return {};
    }
    return document_.pagePointSize(page_index);
}

QImage QtPdfBackend::render_page(int page_index, QSize target_pixel_size) {
    if (page_index < 0 || page_index >= document_.pageCount() || !target_pixel_size.isValid()) {
        return {};
    }

    QPdfDocumentRenderOptions options;
    return document_.render(page_index, target_pixel_size, options);
}

QString QtPdfBackend::error_to_string(QPdfDocument::Error error) {
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
