#include "package/UilPackage.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <array>
#include <optional>

#include <zlib.h>

Q_LOGGING_CATEGORY(logUilPackage, "uil.package")

namespace {
constexpr quint32 localFileHeaderSignature = 0x04034b50;
constexpr quint32 centralDirectoryHeaderSignature = 0x02014b50;
constexpr quint32 endOfCentralDirectorySignature = 0x06054b50;
constexpr quint16 zipStoreMethod = 0;
constexpr quint16 zipDeflateMethod = 8;

struct ZipEntry {
    QString path;
    quint16 compressionMethod = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localHeaderOffset = 0;
};

quint16 readUInt16(const QByteArray& bytes, qsizetype offset) {
    return quint16(uchar(bytes.at(offset)))
        | (quint16(uchar(bytes.at(offset + 1))) << 8);
}

quint32 readUInt32(const QByteArray& bytes, qsizetype offset) {
    return quint32(uchar(bytes.at(offset)))
        | (quint32(uchar(bytes.at(offset + 1))) << 8)
        | (quint32(uchar(bytes.at(offset + 2))) << 16)
        | (quint32(uchar(bytes.at(offset + 3))) << 24);
}

void setError(QString* errorMessage, const QString& message) {
    if (errorMessage) {
        *errorMessage = message;
    }
}

std::optional<qsizetype> findEndOfCentralDirectory(const QByteArray& bytes) {
    if (bytes.size() < 22) {
        return std::nullopt;
    }

    const qsizetype minOffset = std::max<qsizetype>(0, bytes.size() - 65557);
    for (qsizetype offset = bytes.size() - 22; offset >= minOffset; --offset) {
        if (readUInt32(bytes, offset) == endOfCentralDirectorySignature) {
            return offset;
        }
    }

    return std::nullopt;
}

bool isSafeRelativePath(const QString& path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))) {
        return false;
    }

    if (path.contains(QLatin1Char(':'))) {
        return false;
    }

    const QString cleanPath = QDir::cleanPath(path);
    return cleanPath != QStringLiteral(".")
        && !cleanPath.startsWith(QStringLiteral("../"))
        && !cleanPath.contains(QStringLiteral("/../"))
        && cleanPath != QStringLiteral("..");
}

bool parseCentralDirectory(const QByteArray& bytes, QVector<ZipEntry>* entries, QString* errorMessage) {
    const std::optional<qsizetype> eocdOffset = findEndOfCentralDirectory(bytes);
    if (!eocdOffset) {
        setError(errorMessage, QStringLiteral("Missing ZIP end-of-central-directory record"));
        return false;
    }

    const quint16 diskNumber = readUInt16(bytes, *eocdOffset + 4);
    const quint16 centralDirectoryDisk = readUInt16(bytes, *eocdOffset + 6);
    const quint16 entryCount = readUInt16(bytes, *eocdOffset + 10);
    const quint32 centralDirectorySize = readUInt32(bytes, *eocdOffset + 12);
    const quint32 centralDirectoryOffset = readUInt32(bytes, *eocdOffset + 16);
    if (diskNumber != 0 || centralDirectoryDisk != 0) {
        setError(errorMessage, QStringLiteral("Multi-disk ZIP packages are not supported"));
        return false;
    }

    if (centralDirectoryOffset == 0xffffffffu || centralDirectorySize == 0xffffffffu || entryCount == 0xffffu) {
        setError(errorMessage, QStringLiteral("ZIP64 packages are not supported"));
        return false;
    }

    if (qsizetype(centralDirectoryOffset) + qsizetype(centralDirectorySize) > bytes.size()) {
        setError(errorMessage, QStringLiteral("ZIP central directory points outside the package"));
        return false;
    }

    entries->clear();
    qsizetype offset = centralDirectoryOffset;
    for (quint16 i = 0; i < entryCount; ++i) {
        if (offset + 46 > bytes.size() || readUInt32(bytes, offset) != centralDirectoryHeaderSignature) {
            setError(errorMessage, QStringLiteral("Invalid ZIP central directory entry"));
            return false;
        }

        const quint16 generalPurposeFlags = readUInt16(bytes, offset + 8);
        const quint16 compressionMethod = readUInt16(bytes, offset + 10);
        const quint32 compressedSize = readUInt32(bytes, offset + 20);
        const quint32 uncompressedSize = readUInt32(bytes, offset + 24);
        const quint16 fileNameLength = readUInt16(bytes, offset + 28);
        const quint16 extraLength = readUInt16(bytes, offset + 30);
        const quint16 commentLength = readUInt16(bytes, offset + 32);
        const quint32 localHeaderOffset = readUInt32(bytes, offset + 42);
        const qsizetype nameOffset = offset + 46;
        if (nameOffset + fileNameLength > bytes.size()) {
            setError(errorMessage, QStringLiteral("Invalid ZIP entry name"));
            return false;
        }

        if (generalPurposeFlags & 0x0001) {
            setError(errorMessage, QStringLiteral("Encrypted ZIP entries are not supported"));
            return false;
        }

        const QString path = QString::fromUtf8(bytes.mid(nameOffset, fileNameLength));
        if (!path.endsWith(QLatin1Char('/'))) {
            if (!isSafeRelativePath(path)) {
                setError(errorMessage, QStringLiteral("Unsafe path in package: %1").arg(path));
                return false;
            }
            entries->push_back(ZipEntry{QDir::cleanPath(path), compressionMethod, compressedSize, uncompressedSize, localHeaderOffset});
        }

        offset = nameOffset + fileNameLength + extraLength + commentLength;
    }

    return true;
}

QByteArray inflateRawDeflate(const QByteArray& input, quint32 expectedSize, QString* errorMessage) {
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        setError(errorMessage, QStringLiteral("Could not initialize ZIP decompressor"));
        return {};
    }

    QByteArray output;
    output.reserve(int(expectedSize));
    std::array<char, 32768> buffer{};
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = uInt(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        output.append(buffer.data(), int(buffer.size() - stream.avail_out));
    }

    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        setError(errorMessage, QStringLiteral("Could not inflate ZIP entry"));
        return {};
    }

    return output;
}

QByteArray entryPayload(const QByteArray& bytes, const ZipEntry& entry, QString* errorMessage) {
    const qsizetype offset = entry.localHeaderOffset;
    if (offset + 30 > bytes.size() || readUInt32(bytes, offset) != localFileHeaderSignature) {
        setError(errorMessage, QStringLiteral("Invalid local ZIP header for %1").arg(entry.path));
        return {};
    }

    const quint16 fileNameLength = readUInt16(bytes, offset + 26);
    const quint16 extraLength = readUInt16(bytes, offset + 28);
    const qsizetype dataOffset = offset + 30 + fileNameLength + extraLength;
    if (dataOffset + entry.compressedSize > bytes.size()) {
        setError(errorMessage, QStringLiteral("ZIP entry payload points outside the package: %1").arg(entry.path));
        return {};
    }

    const QByteArray compressed = bytes.mid(dataOffset, entry.compressedSize);
    if (entry.compressionMethod == zipStoreMethod) {
        if (compressed.size() != int(entry.uncompressedSize)) {
            setError(errorMessage, QStringLiteral("Stored ZIP entry has an unexpected size: %1").arg(entry.path));
            return {};
        }
        return compressed;
    }

    if (entry.compressionMethod == zipDeflateMethod) {
        QByteArray inflated = inflateRawDeflate(compressed, entry.uncompressedSize, errorMessage);
        if (inflated.size() != int(entry.uncompressedSize)) {
            setError(errorMessage, QStringLiteral("Deflated ZIP entry has an unexpected size: %1").arg(entry.path));
            return {};
        }
        return inflated;
    }

    setError(errorMessage, QStringLiteral("Unsupported ZIP compression method %1 for %2").arg(entry.compressionMethod).arg(entry.path));
    return {};
}

const ZipEntry* findEntry(const QVector<ZipEntry>& entries, const QString& path) {
    const QString cleanPath = QDir::cleanPath(path);
    for (const ZipEntry& entry : entries) {
        if (entry.path == cleanPath) {
            return &entry;
        }
    }

    return nullptr;
}

bool writeFile(const QString& path, const QByteArray& contents, QString* errorMessage) {
    const QFileInfo fileInfo(path);
    QDir dir;
    if (!dir.mkpath(fileInfo.absolutePath())) {
        setError(errorMessage, QStringLiteral("Could not create directory: %1").arg(fileInfo.absolutePath()));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }

    file.write(contents);
    if (!file.commit()) {
        setError(errorMessage, QStringLiteral("Could not finalize %1: %2").arg(path, file.errorString()));
        return false;
    }

    return true;
}
}

bool extractUilPackage(const QString& packagePath, QTemporaryDir& destination, UilPackageOpenResult* result, QString* errorMessage) {
    if (!result) {
        setError(errorMessage, QStringLiteral("Internal error: missing package result"));
        return false;
    }

    QFile packageFile(packagePath);
    if (!packageFile.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Could not open UIL package: %1").arg(packageFile.errorString()));
        return false;
    }

    const QByteArray bytes = packageFile.readAll();
    QVector<ZipEntry> entries;
    if (!parseCentralDirectory(bytes, &entries, errorMessage)) {
        return false;
    }

    const ZipEntry* manifestEntry = findEntry(entries, QStringLiteral("manifest.json"));
    if (!manifestEntry) {
        setError(errorMessage, QStringLiteral("UIL package is missing manifest.json"));
        return false;
    }

    const QByteArray manifestBytes = entryPayload(bytes, *manifestEntry, errorMessage);
    if (manifestBytes.isEmpty()) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
        setError(errorMessage, QStringLiteral("UIL manifest is not valid JSON: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject manifest = manifestDocument.object();
    if (manifest.contains(QStringLiteral("format"))
        && manifest.value(QStringLiteral("format")).toString() != QStringLiteral("uil.presentation-package")) {
        setError(errorMessage, QStringLiteral("Unsupported UIL package format"));
        return false;
    }

    if (manifest.contains(QStringLiteral("format_version"))
        && manifest.value(QStringLiteral("format_version")).toInt() != 1) {
        setError(errorMessage, QStringLiteral("Unsupported UIL package format version"));
        return false;
    }

    const QString entryPdf = QDir::cleanPath(manifest.value(QStringLiteral("entry_pdf")).toString(QStringLiteral("build/presentation.pdf")));
    if (!isSafeRelativePath(entryPdf)) {
        setError(errorMessage, QStringLiteral("Unsafe entry_pdf path in UIL manifest"));
        return false;
    }

    if (!findEntry(entries, entryPdf)) {
        setError(errorMessage, QStringLiteral("UIL package is missing entry PDF: %1").arg(entryPdf));
        return false;
    }

    QStringList movieAssetPaths;
    const QJsonArray movieAssets = manifest.value(QStringLiteral("movie_assets")).toArray();
    for (const QJsonValue& value : movieAssets) {
        if (!value.isObject()) {
            continue;
        }

        const QString rawPath = value.toObject().value(QStringLiteral("path")).toString();
        if (rawPath.isEmpty()) {
            continue;
        }
        const QString path = QDir::cleanPath(rawPath);
        if (!isSafeRelativePath(path)) {
            setError(errorMessage, QStringLiteral("Unsafe movie asset path in UIL manifest: %1").arg(path));
            return false;
        }
        if (!findEntry(entries, path)) {
            setError(errorMessage, QStringLiteral("UIL package is missing movie asset: %1").arg(path));
            return false;
        }
        if (!movieAssetPaths.contains(path)) {
            movieAssetPaths.push_back(path);
        }
    }

    for (const ZipEntry& entry : entries) {
        const QByteArray payload = entryPayload(bytes, entry, errorMessage);
        if (payload.isEmpty() && entry.uncompressedSize > 0) {
            return false;
        }

        const QString outputPath = destination.filePath(entry.path);
        if (!writeFile(outputPath, payload, errorMessage)) {
            return false;
        }
    }

    result->entryPdfRelativePath = entryPdf;
    result->entryPdfPath = destination.filePath(entryPdf);
    result->packageRootPath = destination.path();
    result->movieAssetPaths = movieAssetPaths;
    qCInfo(logUilPackage) << "Extracted UIL package" << packagePath << "entry" << result->entryPdfPath;
    return true;
}
