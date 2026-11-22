#pragma once

#include "figure/interactive_figure.hpp"
#include "molecule/molecule_geometry.hpp"
#include "orbital/atomic_orbital.hpp"
#include "symmetry/molecular_symmetry.hpp"

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief A movie, sound, or rich-media annotation that references an external media file.
 *
 * Common fields of all annotation structs: `page_index` is the zero-based page whose /Annots array
 * references the annotation, or -1 when it is not reachable from the page tree; `object_number` is
 * the PDF indirect-object number; `rect` is the normalized /Rect in PDF user-space units
 * (points, origin at the bottom-left of the page, y pointing up).
 */
struct PdfMediaAnnotation {
    int page_index = -1;
    int object_number = -1;
    /** @brief The /Subtype name (for example `Movie`, `Sound`, `RichMedia`), or `Media`. */
    QString subtype;
    /** @brief The media file name exactly as written in the PDF (/UF, /F, /DOS, /Mac, or /Unix). */
    QString file_name;
    /** @brief Absolute path of the media file; empty when it is undeclared or escapes its root. */
    QString resolved_file_path;
    QRectF rect;
    /** @brief First decoded video frame for resolved MP4 files; null when unavailable. */
    QImage first_frame;

    /** @brief Returns whether a preview frame was decoded for this annotation. */
    bool has_first_frame() const;
    /** @brief Returns whether the resolved path or the file name has an `.mp4` extension. */
    bool is_mp4() const;
};

/** @brief A UIL molecule annotation that references an external XYZ geometry file. */
struct PdfMoleculeAnnotation {
    int page_index = -1;
    int object_number = -1;
    /** @brief The XYZ file name exactly as written in the PDF. */
    QString file_name;
    /** @brief Absolute path of the XYZ file; empty when it is undeclared or escapes its root. */
    QString resolved_file_path;
    QRectF rect;
    /** @brief The loaded geometry; empty when loading failed. */
    MoleculeGeometry geometry;
    /** @brief User-facing reason the molecule is unavailable; empty on success. */
    QString error_message;

    /** @brief Returns whether the geometry loaded without error and the rectangle is non-empty. */
    bool is_ready() const;
};

/** @brief A UIL interactive-figure annotation whose JSON definition is embedded in the PDF. */
struct PdfInteractiveFigureAnnotation {
    int page_index = -1;
    int object_number = -1;
    /** @brief The embedded file's name (/UF, falling back to /F) from its file specification. */
    QString file_name;
    QRectF rect;
    /** @brief The parsed definition; only meaningful when is_ready() returns true. */
    InteractiveFigureDefinition definition;
    /** @brief User-facing reason the figure is unavailable; empty on success. */
    QString error_message;

    /** @brief Returns whether the definition decoded without error and `rect` is non-empty. */
    bool is_ready() const;
};

/** @brief A UIL atomic-orbital annotation whose JSON definition is embedded in the PDF. */
struct PdfAtomicOrbitalAnnotation {
    int page_index = -1;
    int object_number = -1;
    /** @brief The embedded file's name (/UF, falling back to /F) from its file specification. */
    QString file_name;
    QRectF rect;
    /** @brief The parsed definition; only meaningful when is_ready() returns true. */
    AtomicOrbitalDefinition definition;
    /** @brief User-facing reason the orbital is unavailable; empty on success. */
    QString error_message;

    /** @brief Returns whether the definition decoded without error and `rect` is non-empty. */
    bool is_ready() const;
};

/** @brief A UIL molecular-symmetry annotation whose JSON definition is embedded in the PDF. */
struct PdfMolecularSymmetryAnnotation {
    int page_index = -1;
    int object_number = -1;
    /** @brief The embedded file's name (/UF, falling back to /F) from its file specification. */
    QString file_name;
    QRectF rect;
    /** @brief The parsed definition; only meaningful when is_ready() returns true. */
    MolecularSymmetryDefinition definition;
    /** @brief User-facing reason the symmetry player is unavailable; empty on success. */
    QString error_message;

    /** @brief Returns whether the definition decoded without error and `rect` is non-empty. */
    bool is_ready() const;
};

/**
 * @brief All annotations found by scan_pdf_media_annotations().
 *
 * Within each list, annotations reachable from the page tree come first in page order, followed
 * by unattached annotations (page_index -1) in unspecified order.
 */
struct PdfMediaScanResult {
    /** @brief Movie, sound, and rich-media annotations. */
    QVector<PdfMediaAnnotation> annotations;
    QVector<PdfMoleculeAnnotation> molecule_annotations;
    QVector<PdfInteractiveFigureAnnotation> interactive_figure_annotations;
    QVector<PdfAtomicOrbitalAnnotation> atomic_orbital_annotations;
    QVector<PdfMolecularSymmetryAnnotation> molecular_symmetry_annotations;

    /** @brief Returns whether any annotation of any supported kind was found, ready or not. */
    bool has_media() const;
    /** @brief Returns a one-line, human-readable description of every annotation, for logging. */
    QString summary() const;
};

/**
 * @brief Scans a PDF for UIL-supported annotations and loads the assets they reference.
 *
 * Reads the whole file (at most 512 MiB) into memory and parses it with a tolerant, lightweight
 * scanner of directly stored objects and Flate-compressed object streams. It detects:
 * - media annotations (movies, sounds, rich media), whose file names are resolved and whose
 *   resolved MP4 files have their first frame decoded with FFmpeg when FFmpeg is available;
 * - UIL molecule annotations, whose XYZ files are resolved like media and then loaded;
 * - UIL interactive-figure, atomic-orbital, and molecular-symmetry annotations, whose JSON
 *   definitions are decoded from embedded-file streams (at most 4 MiB each) and parsed.
 *
 * External files are resolved, when @p package_root_path is non-empty, only if the PDF names a
 * path listed in the corresponding package asset list, and otherwise relative to the PDF's
 * directory; paths that escape that directory are rejected.
 *
 * The call blocks on file I/O, decompression, and video decoding, so it must run off the GUI
 * thread. Parsing work is bounded (object count, page-tree depth, decompressed bytes, and the
 * number of annotations), so malformed or hostile files cannot exhaust the stack or make the scan
 * run indefinitely; content beyond a limit is ignored with a warning.
 *
 * @param path PDF file to scan.
 * @param package_root_path Directory of the extracted UIL package, or empty for a plain PDF.
 * @param package_movie_asset_paths Package-relative media paths that annotations may reference.
 * @param package_molecule_asset_paths Package-relative XYZ paths that annotations may reference.
 * @return The detected annotations. Failures to open or read the file, oversized files, and
 * internal errors (including allocation failure) are reported only through the `pdf.media`
 * logging category and produce an empty result. Problems with individual annotations are
 * reported through their `error_message`, empty `resolved_file_path`, or null `first_frame`.
 * Never throws.
 */
PdfMediaScanResult scan_pdf_media_annotations(
    const QString& path,
    const QString& package_root_path = QString(),
    const QStringList& package_movie_asset_paths = {},
    const QStringList& package_molecule_asset_paths = {}) noexcept;
