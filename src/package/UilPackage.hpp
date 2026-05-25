#pragma once

#include <QString>
#include <QStringList>

class QTemporaryDir;

struct UilPackageOpenResult {
    QString entryPdfPath;
    QString entryPdfRelativePath;
    QString packageRootPath;
    QStringList movieAssetPaths;
};

bool extractUilPackage(const QString& packagePath, QTemporaryDir& destination, UilPackageOpenResult* result, QString* errorMessage = nullptr);
