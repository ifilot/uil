#pragma once

#include "figure/interactive_figure.hpp"
#include "molecule/molecule_geometry.hpp"

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

struct PdfMoleculeAnnotation {
    int page_index = -1;
    int object_number = -1;
    QString file_name;
    QString resolved_file_path;
    QRectF rect;
    MoleculeGeometry geometry;
    QString error_message;

    /** @brief Returns whether a valid geometry was loaded for this annotation. */
    bool is_ready() const;
};

struct PdfInteractiveFigureAnnotation {
    int page_index = -1;
    int object_number = -1;
    QString file_name;
    QRectF rect;
    InteractiveFigureDefinition definition;
    QString error_message;

    /** @brief Returns whether an embedded interactive figure was decoded and validated. */
    bool is_ready() const;
};

struct PdfMediaScanResult {
    QVector<PdfMediaAnnotation> annotations;
    QVector<PdfMoleculeAnnotation> molecule_annotations;
    QVector<PdfInteractiveFigureAnnotation> interactive_figure_annotations;

    /** @brief Returns whether the scan found at least one interactive annotation. */
    bool has_media() const;
    /** @brief Returns a concise user-facing summary of the scan. */
    QString summary() const;
};

/** @brief Scans a PDF for media annotations and resolves packaged movie assets. */
PdfMediaScanResult scan_pdf_media_annotations(
    const QString& path,
    const QString& package_root_path = QString(),
    const QStringList& package_movie_asset_paths = {},
    const QStringList& package_molecule_asset_paths = {});
