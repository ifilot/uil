#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>

class QTemporaryDir;

struct UilPackageOpenResult {
    QString entryPdfPath;
    QString entryPdfRelativePath;
    QString packageRootPath;
    QStringList movieAssetPaths;
    QHash<int, QString> overlayImagePaths;
    QSet<int> hiddenOverlayPages;
    bool overlaysGloballyVisible = true;
};

bool extractUilPackage(const QString& packagePath, QTemporaryDir& destination, UilPackageOpenResult* result, QString* errorMessage = nullptr);

bool writeUilPackage(
    const QString& packagePath,
    const QString& sourcePdfPath,
    const QString& entryPdfRelativePath,
    const QString& assetRootPath,
    const QStringList& movieAssetPaths,
    const QHash<int, QImage>& overlayImages,
    const QSet<int>& hiddenOverlayPages,
    bool overlaysGloballyVisible,
    QString* errorMessage = nullptr);
