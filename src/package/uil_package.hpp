#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>

class QTemporaryDir;

/**
 * @brief Largest zero-based page index stored for overlays and hidden-overlay pages.
 *
 * Indices above this cap (100000 pages) are ignored when reading and writing packages; it keeps
 * hostile manifests from producing absurd page numbers and keeps `page_index + 1` overflow-free.
 */
constexpr int kMaximumUilPackagePageIndex = 99'999;

/** @brief Describes the files extracted from a UIL package by extract_uil_package(). */
struct UilPackageOpenResult {
    /** @brief Absolute path of the extracted entry PDF. */
    QString entry_pdf_path;
    /** @brief Normalized package-relative path of the entry PDF. */
    QString entry_pdf_relative_path;
    /** @brief Extraction root; asset paths below are relative to it. */
    QString package_root_path;
    /** @brief Package-relative paths of extracted movie assets, without duplicates. */
    QStringList movie_asset_paths;
    /** @brief Package-relative paths of extracted molecule (XYZ) assets, without duplicates. */
    QStringList molecule_asset_paths;
    /**
     * @brief Package-relative overlay PNG paths keyed by zero-based page index.
     *
     * Indices are within [0, #kMaximumUilPackagePageIndex] but are not checked against the
     * PDF's page count.
     */
    QHash<int, QString> overlay_image_paths;
    /** @brief Zero-based pages whose overlays start hidden, within the same bounds. */
    QSet<int> hidden_overlay_pages;
    /** @brief Whether overlays are shown at all; true when the manifest omits the flag. */
    bool overlays_globally_visible = true;
};

/**
 * @brief Extracts a UIL package into @p destination and describes its contents in @p result.
 *
 * A UIL package is a ZIP32 archive (stored or deflated entries) containing `manifest.json`
 * (format `uil.presentation-package`, versions 1 and 2), the entry PDF, and the movie,
 * molecule and overlay files it lists. The archive is validated before anything is written:
 * unsafe or duplicate paths, encrypted, multi-disk and ZIP64 archives, CRC mismatches, and
 * entries exceeding the size, count or compression-ratio limits are rejected. Only files
 * referenced by the manifest are extracted. Overlay and hidden-page entries with negative
 * indices or indices above #kMaximumUilPackagePageIndex are ignored.
 *
 * @param package_path Package file to read (at most 1 GiB).
 * @param destination Temporary directory that receives the extracted files.
 * @param result Receives the extracted paths; left unchanged on failure.
 * @param error_message Optional; receives a human-readable reason on failure.
 * @return True when the package was extracted completely. Files written before a failure are
 *         left in @p destination.
 */
bool extract_uil_package(const QString& package_path, QTemporaryDir& destination, UilPackageOpenResult* result, QString* error_message = nullptr);

/**
 * @brief Writes a UIL package containing a PDF, interactive assets, and overlays.
 *
 * Writes an uncompressed (stored) ZIP32 archive with a format-version-2 manifest. The file is
 * replaced atomically through QSaveFile, so a failed write leaves an existing package intact.
 *
 * @param package_path Output path; its parent directory is created when needed.
 * @param source_pdf_path PDF to embed.
 * @param entry_pdf_relative_path Package path of the PDF; empty selects
 *        `build/presentation.pdf`.
 * @param asset_root_path Directory against which the asset paths are resolved.
 * @param movie_asset_paths Relative movie asset paths to embed.
 * @param molecule_asset_paths Relative molecule asset paths to embed.
 * @param overlay_images Overlay images keyed by zero-based page index, stored as
 *        `overlays/page-NNNN.png` (one-based). Null images and indices outside
 *        [0, #kMaximumUilPackagePageIndex] are skipped.
 * @param hidden_overlay_pages Pages whose overlays start hidden; out-of-range indices are
 *        skipped.
 * @param overlays_globally_visible Global overlay visibility flag.
 * @param error_message Optional; receives a human-readable reason on failure.
 * @return True when the package was written. Unsafe, duplicate (case-insensitive), missing or
 *         oversized inputs fail the whole write.
 */
bool write_uil_package(
    const QString& package_path,
    const QString& source_pdf_path,
    const QString& entry_pdf_relative_path,
    const QString& asset_root_path,
    const QStringList& movie_asset_paths,
    const QStringList& molecule_asset_paths,
    const QHash<int, QImage>& overlay_images,
    const QSet<int>& hidden_overlay_pages,
    bool overlays_globally_visible,
    QString* error_message = nullptr);
