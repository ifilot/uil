#pragma once

#include "pdf_backend.hpp"

#include <QBuffer>
#include <QPdfDocument>

class QtPdfBackend final : public PdfBackend {
public:
    /** @brief Constructs a Qt PDF backend. */
    QtPdfBackend();
    /** @brief Destroys the Qt PDF backend. */
    ~QtPdfBackend() override;

    /** @copydoc PdfBackend::open */
    bool open(const QString& path, QString* error_message = nullptr) override;
    /** @copydoc PdfBackend::open_data */
    bool open_data(const QByteArray& data, QString* error_message = nullptr) override;
    /** @copydoc PdfBackend::page_count */
    int page_count() const override;
    /** @copydoc PdfBackend::page_size_points */
    QSizeF page_size_points(int page_index) const override;
    /** @copydoc PdfBackend::render_page */
    QImage render_page(int page_index, QSize target_pixel_size) override;

private:
    /** @brief Converts a Qt PDF error value to a readable message. */
    static QString error_to_string(QPdfDocument::Error error);
    /** @brief Reports the outcome of a load attempt for @p source. */
    bool finish_open(QPdfDocument::Error error, const QString& source, QString* error_message);

    // Declared before document_ so the document is destroyed while its device still exists.
    QBuffer buffer_;
    QPdfDocument document_;
};
