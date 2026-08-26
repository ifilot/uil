#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>

class QTemporaryDir;

struct UilPackageOpenResult {
    QString entry_pdf_path;
    QString entry_pdf_relative_path;
    QString package_root_path;
    QStringList movie_asset_paths;
    QHash<int, QString> overlay_image_paths;
    QSet<int> hidden_overlay_pages;
    bool overlays_globally_visible = true;
};

/** @brief Extracts a UIL package into @p destination and describes its contents in @p result. */
bool extract_uil_package(const QString& package_path, QTemporaryDir& destination, UilPackageOpenResult* result, QString* error_message = nullptr);

/** @brief Writes a UIL package containing a PDF, media assets, and annotation overlays. */
bool write_uil_package(
    const QString& package_path,
    const QString& source_pdf_path,
    const QString& entry_pdf_relative_path,
    const QString& asset_root_path,
    const QStringList& movie_asset_paths,
    const QHash<int, QImage>& overlay_images,
    const QSet<int>& hidden_overlay_pages,
    bool overlays_globally_visible,
    QString* error_message = nullptr);
