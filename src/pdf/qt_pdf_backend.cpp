#include "qt_pdf_backend.hpp"

#include <QLoggingCategory>
#include <QPdfDocumentRenderOptions>

Q_LOGGING_CATEGORY(log_pdf, "pdf")

QtPdfBackend::QtPdfBackend() = default;
QtPdfBackend::~QtPdfBackend() = default;

bool QtPdfBackend::open(const QString& path, QString* error_message) {
    document_.close();
    buffer_.close();
    return finish_open(document_.load(path), path, error_message);
}

bool QtPdfBackend::open_data(const QByteArray& data, QString* error_message) {
    document_.close();
    buffer_.close();
    buffer_.setData(data);
    if (!buffer_.open(QIODevice::ReadOnly)) {
        if (error_message) {
            *error_message = QStringLiteral("Could not read PDF data");
        }
        return false;
    }

    // QBuffer is random access, so QPdfDocument parses it synchronously here.
    document_.load(&buffer_);
    const QPdfDocument::Error error = document_.status() == QPdfDocument::Status::Ready
        ? QPdfDocument::Error::None
        : (document_.error() == QPdfDocument::Error::None ? QPdfDocument::Error::Unknown
                                                          : document_.error());
    return finish_open(error, QStringLiteral("<memory>"), error_message);
}

bool QtPdfBackend::finish_open(
    QPdfDocument::Error error,
    const QString& source,
    QString* error_message) {
    if (error != QPdfDocument::Error::None) {
        if (error_message) {
            *error_message = error_to_string(error);
        }
        qCWarning(log_pdf) << "Failed to open PDF" << source << error_to_string(error);
        return false;
    }

    qCInfo(log_pdf) << "Opened PDF" << source << "pages:" << document_.pageCount();
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
    QImage image = document_.render(page_index, target_pixel_size, options);
    // QtPdf returns non-premultiplied ARGB32, which QPainter would otherwise convert on every
    // paint. Convert once here, off the GUI thread.
    if (!image.isNull() && image.format() != QImage::Format_ARGB32_Premultiplied) {
        image.convertTo(QImage::Format_ARGB32_Premultiplied);
    }
    return image;
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
