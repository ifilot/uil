#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QString>

class PdfBackend {
public:
    /** @brief Destroys the PDF backend. */
    virtual ~PdfBackend() = default;

    /** @brief Opens a PDF document and reports failures through @p error_message. */
    virtual bool open(const QString& path, QString* error_message = nullptr) = 0;
    /** @brief Returns the number of pages in the open document. */
    virtual int page_count() const = 0;
    /** @brief Returns the dimensions of a page in PDF points. */
    virtual QSizeF page_size_points(int page_index) const = 0;

    /** @brief Renders a page at the requested pixel size. */
    virtual QImage render_page(int page_index, QSize target_pixel_size) = 0;
};
