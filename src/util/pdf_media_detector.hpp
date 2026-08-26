#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

struct PdfMediaAnnotation {
    int page_index = -1;
    int object_number = -1;
    QString subtype;
    QString fileName;
    QString resolved_file_path;
    QRectF rect;
    QImage first_frame;

    /** @brief Returns whether a preview frame was decoded for this annotation. */
    bool has_first_frame() const;
    /** @brief Returns whether this annotation references an MP4 asset. */
    bool is_mp4() const;
};

struct PdfMediaScanResult {
    QVector<PdfMediaAnnotation> annotations;

    /** @brief Returns whether the scan found at least one media annotation. */
    bool has_media() const;
    /** @brief Returns a concise user-facing summary of the scan. */
    QString summary() const;
};

/** @brief Scans a PDF for media annotations and resolves packaged movie assets. */
PdfMediaScanResult scan_pdf_media_annotations(
    const QString& path,
    const QString& package_root_path = QString(),
    const QStringList& package_movie_asset_paths = {});
