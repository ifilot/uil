#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QString>

/**
 * @brief Abstract page source for one open PDF document.
 *
 * An instance is not thread-safe and must be confined to one thread. The render scheduler
 * creates one backend per worker thread, while AppController owns a separate GUI-thread
 * instance for page geometry and export.
 */
class PdfBackend {
public:
    /** @brief Destroys the PDF backend. */
    virtual ~PdfBackend() = default;

    /**
     * @brief Opens the PDF file at @p path, replacing any previously open document.
     * @param[in] path File to open. The file may be read lazily while the document stays open.
     * @param[out] error_message Receives a readable reason on failure; may be null.
     * @return True when the document is open and has been parsed.
     */
    virtual bool open(const QString& path, QString* error_message = nullptr) = 0;
    /**
     * @brief Opens a PDF from an in-memory copy, replacing any previously open document.
     *
     * The backend keeps a shallow, implicitly shared copy of @p data, so later changes to the
     * file the bytes came from cannot affect rendering.
     * @param[in] data Complete PDF file contents.
     * @param[out] error_message Receives a readable reason on failure; may be null.
     * @return True when the document is open and has been parsed.
     */
    virtual bool open_data(const QByteArray& data, QString* error_message = nullptr) = 0;
    /** @brief Returns the number of pages in the open document, or 0 when none is open. */
    virtual int page_count() const = 0;
    /** @brief Returns a page's size in PDF points, or an empty size for an invalid index. */
    virtual QSizeF page_size_points(int page_index) const = 0;

    /**
     * @brief Renders a page at the requested pixel size.
     * @return A Format_ARGB32_Premultiplied image, or a null image for an invalid index or
     *         size or when rendering fails.
     */
    virtual QImage render_page(int page_index, QSize target_pixel_size) = 0;
};
