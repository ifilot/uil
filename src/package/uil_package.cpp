#include "package/uil_package.hpp"

#include <QDir>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

#include <zlib.h>

Q_LOGGING_CATEGORY(logUilPackage, "uil.package")

namespace {
constexpr quint32 kLocalFileHeaderSignature = 0x04034b50;
constexpr quint32 kCentralDirectoryHeaderSignature = 0x02014b50;
constexpr quint32 kEndOfCentralDirectorySignature = 0x06054b50;
constexpr quint16 kZipStoreMethod = 0;
constexpr quint16 kZipDeflateMethod = 8;
constexpr quint64 kMaximumPackageBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaximumEntryBytes = 512ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaximumExtractedBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr quint32 kMaximumManifestBytes = 4U * 1024U * 1024U;
constexpr quint32 kMaximumCompressionRatio = 200;
constexpr int kMaximumEntryCount = 4096;

struct ZipEntry {
    QString path;
    quint16 compressionMethod = 0;
    quint32 crc = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localHeaderOffset = 0;
};

/** @brief Normalizes separators and removes redundant path components. */
QString normalized_package_path(const QString& path) {
    QString portable_path = path;
    portable_path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(portable_path);
}

struct ZipWriteEntry {
    QString path;
    QByteArray data;
    quint32 crc = 0;
    quint32 localHeaderOffset = 0;
};

/** @brief Reads a little-endian 16-bit integer from a byte array. */
quint16 read_uint16(const QByteArray& bytes, qsizetype offset) {
    return quint16(uchar(bytes.at(offset)))
        | (quint16(uchar(bytes.at(offset + 1))) << 8);
}

/** @brief Reads a little-endian 32-bit integer from a byte array. */
quint32 read_uint32(const QByteArray& bytes, qsizetype offset) {
    return quint32(uchar(bytes.at(offset)))
        | (quint32(uchar(bytes.at(offset + 1))) << 8)
        | (quint32(uchar(bytes.at(offset + 2))) << 16)
        | (quint32(uchar(bytes.at(offset + 3))) << 24);
}

/** @brief Stores an error message when an output pointer was supplied. */
void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

/** @brief Locates the ZIP end-of-central-directory record. */
std::optional<qsizetype> find_end_of_central_directory(const QByteArray& bytes) {
    if (bytes.size() < 22) {
        return std::nullopt;
    }

    const qsizetype minOffset = std::max<qsizetype>(0, bytes.size() - 65557);
    for (qsizetype offset = bytes.size() - 22; offset >= minOffset; --offset) {
        if (read_uint32(bytes, offset) == kEndOfCentralDirectorySignature) {
            return offset;
        }
    }

    return std::nullopt;
}

/** @brief Returns whether a package path is relative and traversal-safe. */
bool is_safe_relative_path(const QString& path) {
    if (path.isEmpty() || path.contains(QChar::Null)) {
        return false;
    }

    const QString normalized_path = normalized_package_path(path);
    if (normalized_path.startsWith(QLatin1Char('/'))
        || normalized_path.contains(QLatin1Char(':'))) {
        return false;
    }

    return normalized_path != QStringLiteral(".")
        && !normalized_path.startsWith(QStringLiteral("../"))
        && !normalized_path.contains(QStringLiteral("/../"))
        && normalized_path != QStringLiteral("..");
}

/** @brief Parses ZIP central-directory entries from package bytes. */
bool parse_central_directory(const QByteArray& bytes, QVector<ZipEntry>* entries, QString* error_message) {
    const std::optional<qsizetype> eocdOffset = find_end_of_central_directory(bytes);
    if (!eocdOffset) {
        set_error(error_message, QStringLiteral("Missing ZIP end-of-central-directory record"));
        return false;
    }

    const quint16 diskNumber = read_uint16(bytes, *eocdOffset + 4);
    const quint16 centralDirectoryDisk = read_uint16(bytes, *eocdOffset + 6);
    const quint16 diskEntryCount = read_uint16(bytes, *eocdOffset + 8);
    const quint16 entryCount = read_uint16(bytes, *eocdOffset + 10);
    const quint32 centralDirectorySize = read_uint32(bytes, *eocdOffset + 12);
    const quint32 centralDirectoryOffset = read_uint32(bytes, *eocdOffset + 16);
    const quint16 comment_length = read_uint16(bytes, *eocdOffset + 20);
    if (diskNumber != 0 || centralDirectoryDisk != 0 || diskEntryCount != entryCount) {
        set_error(error_message, QStringLiteral("Multi-disk ZIP packages are not supported"));
        return false;
    }

    if (*eocdOffset + 22 + comment_length != bytes.size()) {
        set_error(error_message, QStringLiteral("ZIP end record has an invalid comment or trailing data"));
        return false;
    }

    if (centralDirectoryOffset == 0xffffffffu || centralDirectorySize == 0xffffffffu || entryCount == 0xffffu) {
        set_error(error_message, QStringLiteral("ZIP64 packages are not supported"));
        return false;
    }

    if (entryCount > kMaximumEntryCount) {
        set_error(error_message, QStringLiteral("UIL package contains too many files"));
        return false;
    }

    if (qsizetype(centralDirectoryOffset) + qsizetype(centralDirectorySize) > bytes.size()) {
        set_error(error_message, QStringLiteral("ZIP central directory points outside the package"));
        return false;
    }

    entries->clear();
    QSet<QString> normalized_paths;
    quint64 total_uncompressed_size = 0;
    qsizetype offset = centralDirectoryOffset;
    const qsizetype central_directory_end = qsizetype(centralDirectoryOffset) + qsizetype(centralDirectorySize);
    for (quint16 i = 0; i < entryCount; ++i) {
        if (offset + 46 > central_directory_end || read_uint32(bytes, offset) != kCentralDirectoryHeaderSignature) {
            set_error(error_message, QStringLiteral("Invalid ZIP central directory entry"));
            return false;
        }

        const quint16 generalPurposeFlags = read_uint16(bytes, offset + 8);
        const quint16 compressionMethod = read_uint16(bytes, offset + 10);
        const quint32 checksum = read_uint32(bytes, offset + 16);
        const quint32 compressedSize = read_uint32(bytes, offset + 20);
        const quint32 uncompressedSize = read_uint32(bytes, offset + 24);
        const quint16 fileNameLength = read_uint16(bytes, offset + 28);
        const quint16 extraLength = read_uint16(bytes, offset + 30);
        const quint16 commentLength = read_uint16(bytes, offset + 32);
        const quint32 localHeaderOffset = read_uint32(bytes, offset + 42);
        const qsizetype nameOffset = offset + 46;
        const qsizetype next_offset = nameOffset + fileNameLength + extraLength + commentLength;
        if (next_offset > central_directory_end) {
            set_error(error_message, QStringLiteral("Invalid ZIP entry name"));
            return false;
        }

        if (generalPurposeFlags & 0x0001) {
            set_error(error_message, QStringLiteral("Encrypted ZIP entries are not supported"));
            return false;
        }

        if (compressionMethod != kZipStoreMethod && compressionMethod != kZipDeflateMethod) {
            set_error(error_message, QStringLiteral("Unsupported ZIP compression method"));
            return false;
        }

        if (uncompressedSize > kMaximumEntryBytes) {
            set_error(error_message, QStringLiteral("UIL package entry exceeds the extraction limit"));
            return false;
        }
        total_uncompressed_size += uncompressedSize;
        if (total_uncompressed_size > kMaximumExtractedBytes) {
            set_error(error_message, QStringLiteral("UIL package exceeds the total extraction limit"));
            return false;
        }
        if (compressionMethod == kZipDeflateMethod
            && uncompressedSize > 1024 * 1024
            && (compressedSize == 0
                || quint64(uncompressedSize) > quint64(compressedSize) * kMaximumCompressionRatio)) {
            set_error(error_message, QStringLiteral("UIL package entry has an unsafe compression ratio"));
            return false;
        }

        const QByteArray encoded_path = bytes.mid(nameOffset, fileNameLength);
        const QString path = QString::fromUtf8(encoded_path);
        if (path.toUtf8() != encoded_path) {
            set_error(error_message, QStringLiteral("ZIP entry name is not valid UTF-8"));
            return false;
        }
        if (path.endsWith(QLatin1Char('/'))) {
            QString directory_path = path;
            directory_path.chop(1);
            if (!is_safe_relative_path(directory_path)) {
                set_error(error_message, QStringLiteral("Unsafe directory path in package: %1").arg(path));
                return false;
            }
        } else {
            if (!is_safe_relative_path(path)) {
                set_error(error_message, QStringLiteral("Unsafe path in package: %1").arg(path));
                return false;
            }
            const QString normalized_path = normalized_package_path(path);
            const QString collision_key = normalized_path.toCaseFolded();
            if (normalized_paths.contains(collision_key)) {
                set_error(error_message, QStringLiteral("Duplicate path in UIL package: %1").arg(normalized_path));
                return false;
            }
            normalized_paths.insert(collision_key);
            entries->push_back(ZipEntry{
                normalized_path,
                compressionMethod,
                checksum,
                compressedSize,
                uncompressedSize,
                localHeaderOffset});
        }

        offset = next_offset;
    }

    if (offset != central_directory_end) {
        set_error(error_message, QStringLiteral("ZIP central directory size does not match its entries"));
        return false;
    }

    return true;
}

/** @brief Inflates a raw DEFLATE stream from a ZIP entry. */
QByteArray inflate_raw_deflate(const QByteArray& input, quint32 expected_size, QString* error_message) {
    if (expected_size > kMaximumEntryBytes) {
        set_error(error_message, QStringLiteral("ZIP entry exceeds the decompression limit"));
        return {};
    }
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        set_error(error_message, QStringLiteral("Could not initialize ZIP decompressor"));
        return {};
    }

    QByteArray output;
    output.reserve(int(expected_size));
    std::array<char, 32768> buffer{};
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = uInt(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        output.append(buffer.data(), int(buffer.size() - stream.avail_out));
        if (quint64(output.size()) > expected_size || quint64(output.size()) > kMaximumEntryBytes) {
            inflateEnd(&stream);
            set_error(error_message, QStringLiteral("ZIP entry expanded beyond its declared size"));
            return {};
        }
    }

    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        set_error(error_message, QStringLiteral("Could not inflate ZIP entry"));
        return {};
    }

    return output;
}

/** @brief Extracts and decompresses the payload for one ZIP entry. */
QByteArray entry_payload(const QByteArray& bytes, const ZipEntry& entry, QString* error_message) {
    const qsizetype offset = entry.localHeaderOffset;
    if (offset + 30 > bytes.size() || read_uint32(bytes, offset) != kLocalFileHeaderSignature) {
        set_error(error_message, QStringLiteral("Invalid local ZIP header for %1").arg(entry.path));
        return {};
    }

    const quint16 fileNameLength = read_uint16(bytes, offset + 26);
    const quint16 extraLength = read_uint16(bytes, offset + 28);
    const quint16 local_compression_method = read_uint16(bytes, offset + 8);
    if (local_compression_method != entry.compressionMethod) {
        set_error(error_message, QStringLiteral("ZIP compression method mismatch for %1").arg(entry.path));
        return {};
    }
    const QByteArray local_encoded_path = bytes.mid(offset + 30, fileNameLength);
    const QString local_decoded_path = QString::fromUtf8(local_encoded_path);
    const QString local_path = normalized_package_path(local_decoded_path);
    if (local_path != entry.path || local_decoded_path.toUtf8() != local_encoded_path) {
        set_error(error_message, QStringLiteral("ZIP local and central names differ for %1").arg(entry.path));
        return {};
    }
    const qsizetype dataOffset = offset + 30 + fileNameLength + extraLength;
    if (dataOffset + entry.compressedSize > bytes.size()) {
        set_error(error_message, QStringLiteral("ZIP entry payload points outside the package: %1").arg(entry.path));
        return {};
    }

    const QByteArray compressed = bytes.mid(dataOffset, entry.compressedSize);
    if (entry.compressionMethod == kZipStoreMethod) {
        if (compressed.size() != int(entry.uncompressedSize)) {
            set_error(error_message, QStringLiteral("Stored ZIP entry has an unexpected size: %1").arg(entry.path));
            return {};
        }
        const quint32 checksum = crc32(
            0L,
            reinterpret_cast<const Bytef*>(compressed.constData()),
            uInt(compressed.size()));
        if (checksum != entry.crc) {
            set_error(error_message, QStringLiteral("ZIP checksum mismatch for %1").arg(entry.path));
            return {};
        }
        return compressed;
    }

    if (entry.compressionMethod == kZipDeflateMethod) {
        QByteArray inflated = inflate_raw_deflate(compressed, entry.uncompressedSize, error_message);
        if (inflated.size() != int(entry.uncompressedSize)) {
            set_error(error_message, QStringLiteral("Deflated ZIP entry has an unexpected size: %1").arg(entry.path));
            return {};
        }
        const quint32 checksum = crc32(
            0L,
            reinterpret_cast<const Bytef*>(inflated.constData()),
            uInt(inflated.size()));
        if (checksum != entry.crc) {
            set_error(error_message, QStringLiteral("ZIP checksum mismatch for %1").arg(entry.path));
            return {};
        }
        return inflated;
    }

    set_error(error_message, QStringLiteral("Unsupported ZIP compression method %1 for %2").arg(entry.compressionMethod).arg(entry.path));
    return {};
}

/** @brief Finds a ZIP entry by normalized package path. */
const ZipEntry* find_entry(const QVector<ZipEntry>& entries, const QString& path) {
    const QString cleanPath = normalized_package_path(path);
    for (const ZipEntry& entry : entries) {
        if (entry.path == cleanPath) {
            return &entry;
        }
    }

    return nullptr;
}

/** @brief Writes bytes to a file, creating its parent directory as needed. */
bool write_file(const QString& path, const QByteArray& contents, QString* error_message) {
    const QFileInfo fileInfo(path);
    QDir dir;
    if (!dir.mkpath(fileInfo.absolutePath())) {
        set_error(error_message, QStringLiteral("Could not create directory: %1").arg(fileInfo.absolutePath()));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        set_error(error_message, QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }

    if (file.write(contents) != contents.size()) {
        set_error(error_message, QStringLiteral("Could not fully write %1: %2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        set_error(error_message, QStringLiteral("Could not finalize %1: %2").arg(path, file.errorString()));
        return false;
    }

    return true;
}

/** @brief Appends a little-endian 16-bit integer to a byte array. */
void append_uint16(QByteArray* bytes, quint16 value) {
    bytes->append(char(value & 0xff));
    bytes->append(char((value >> 8) & 0xff));
}

/** @brief Appends a little-endian 32-bit integer to a byte array. */
void append_uint32(QByteArray* bytes, quint32 value) {
    bytes->append(char(value & 0xff));
    bytes->append(char((value >> 8) & 0xff));
    bytes->append(char((value >> 16) & 0xff));
    bytes->append(char((value >> 24) & 0xff));
}

/** @brief Reads an entire file into a byte array. */
bool read_file_bytes(const QString& path, QByteArray* contents, QString* error_message) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        set_error(error_message, QStringLiteral("Could not read %1: %2").arg(path, file.errorString()));
        return false;
    }

    if (quint64(file.size()) > kMaximumEntryBytes) {
        set_error(error_message, QStringLiteral("Input file exceeds the package entry limit: %1").arg(path));
        return false;
    }

    *contents = file.readAll();
    if (file.error() != QFileDevice::NoError || contents->size() != file.size()) {
        set_error(error_message, QStringLiteral("Could not fully read %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

/** @brief Adds an in-memory file to a pending ZIP archive. */
bool add_zip_entry(QVector<ZipWriteEntry>* entries, const QString& path, const QByteArray& data, QString* error_message) {
    const QString cleanPath = normalized_package_path(path);
    if (!is_safe_relative_path(cleanPath)) {
        set_error(error_message, QStringLiteral("Unsafe package output path: %1").arg(path));
        return false;
    }
    if (quint64(data.size()) > kMaximumEntryBytes) {
        set_error(error_message, QStringLiteral("Package entry is too large: %1").arg(cleanPath));
        return false;
    }
    if (entries->size() >= kMaximumEntryCount) {
        set_error(error_message, QStringLiteral("UIL package contains too many files"));
        return false;
    }

    const auto existing = std::find_if(entries->cbegin(), entries->cend(), [&cleanPath](const ZipWriteEntry& entry) {
        return entry.path.compare(cleanPath, Qt::CaseInsensitive) == 0;
    });
    if (existing != entries->cend()) {
        set_error(error_message, QStringLiteral("Duplicate package entry: %1").arg(cleanPath));
        return false;
    }

    const quint32 checksum = crc32(0L, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size()));
    entries->push_back(ZipWriteEntry{cleanPath, data, checksum, 0});
    return true;
}

/** @brief Adds a filesystem file to a pending ZIP archive. */
bool add_file_zip_entry(QVector<ZipWriteEntry>* entries, const QString& package_path, const QString& filePath, QString* error_message) {
    QByteArray data;
    if (!read_file_bytes(filePath, &data, error_message)) {
        return false;
    }

    return add_zip_entry(entries, package_path, data, error_message);
}

/** @brief Encodes a Qt image as PNG bytes. */
QByteArray png_bytes_for_image(const QImage& image, QString* error_message) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        set_error(error_message, QStringLiteral("Could not encode overlay image"));
        return {};
    }

    return bytes;
}

/** @brief Serializes pending entries as a ZIP archive. */
bool write_zip_file(const QString& path, QVector<ZipWriteEntry> entries, QString* error_message) {
    QByteArray output;
    for (ZipWriteEntry& entry : entries) {
        if (output.size() > std::numeric_limits<quint32>::max()) {
            set_error(error_message, QStringLiteral("Package is too large for ZIP32"));
            return false;
        }

        entry.localHeaderOffset = quint32(output.size());
        const QByteArray name = entry.path.toUtf8();
        append_uint32(&output, kLocalFileHeaderSignature);
        append_uint16(&output, 20);
        append_uint16(&output, 0x0800);
        append_uint16(&output, kZipStoreMethod);
        append_uint16(&output, 0);
        append_uint16(&output, 0);
        append_uint32(&output, entry.crc);
        append_uint32(&output, quint32(entry.data.size()));
        append_uint32(&output, quint32(entry.data.size()));
        append_uint16(&output, quint16(name.size()));
        append_uint16(&output, 0);
        output.append(name);
        output.append(entry.data);
    }

    if (output.size() > std::numeric_limits<quint32>::max()) {
        set_error(error_message, QStringLiteral("Package is too large for ZIP32"));
        return false;
    }

    const quint32 centralDirectoryOffset = quint32(output.size());
    for (const ZipWriteEntry& entry : entries) {
        const QByteArray name = entry.path.toUtf8();
        append_uint32(&output, kCentralDirectoryHeaderSignature);
        append_uint16(&output, 20);
        append_uint16(&output, 20);
        append_uint16(&output, 0x0800);
        append_uint16(&output, kZipStoreMethod);
        append_uint16(&output, 0);
        append_uint16(&output, 0);
        append_uint32(&output, entry.crc);
        append_uint32(&output, quint32(entry.data.size()));
        append_uint32(&output, quint32(entry.data.size()));
        append_uint16(&output, quint16(name.size()));
        append_uint16(&output, 0);
        append_uint16(&output, 0);
        append_uint16(&output, 0);
        append_uint16(&output, 0);
        append_uint32(&output, 0);
        append_uint32(&output, entry.localHeaderOffset);
        output.append(name);
    }

    const quint32 centralDirectorySize = quint32(output.size()) - centralDirectoryOffset;
    append_uint32(&output, kEndOfCentralDirectorySignature);
    append_uint16(&output, 0);
    append_uint16(&output, 0);
    append_uint16(&output, quint16(entries.size()));
    append_uint16(&output, quint16(entries.size()));
    append_uint32(&output, centralDirectorySize);
    append_uint32(&output, centralDirectoryOffset);
    append_uint16(&output, 0);

    const QFileInfo outputInfo(path);
    QDir dir;
    if (!dir.mkpath(outputInfo.absolutePath())) {
        set_error(error_message, QStringLiteral("Could not create directory: %1").arg(outputInfo.absolutePath()));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        set_error(error_message, QStringLiteral("Could not write UIL package: %1").arg(file.errorString()));
        return false;
    }
    file.write(output);
    if (!file.commit()) {
        set_error(error_message, QStringLiteral("Could not finalize UIL package: %1").arg(file.errorString()));
        return false;
    }

    return true;
}
}

bool extract_uil_package(const QString& package_path, QTemporaryDir& destination, UilPackageOpenResult* result, QString* error_message) {
    if (!result) {
        set_error(error_message, QStringLiteral("Internal error: missing package result"));
        return false;
    }

    QFile packageFile(package_path);
    if (!packageFile.open(QIODevice::ReadOnly)) {
        set_error(error_message, QStringLiteral("Could not open UIL package: %1").arg(packageFile.errorString()));
        return false;
    }

    if (quint64(packageFile.size()) > kMaximumPackageBytes) {
        set_error(error_message, QStringLiteral("UIL package exceeds the maximum supported size"));
        return false;
    }

    const QByteArray bytes = packageFile.readAll();
    if (packageFile.error() != QFileDevice::NoError || bytes.size() != packageFile.size()) {
        set_error(error_message, QStringLiteral("Could not completely read UIL package: %1").arg(packageFile.errorString()));
        return false;
    }
    QVector<ZipEntry> entries;
    if (!parse_central_directory(bytes, &entries, error_message)) {
        return false;
    }

    const ZipEntry* manifestEntry = find_entry(entries, QStringLiteral("manifest.json"));
    if (!manifestEntry) {
        set_error(error_message, QStringLiteral("UIL package is missing manifest.json"));
        return false;
    }
    if (manifestEntry->uncompressedSize > kMaximumManifestBytes) {
        set_error(error_message, QStringLiteral("UIL manifest exceeds the size limit"));
        return false;
    }

    const QByteArray manifestBytes = entry_payload(bytes, *manifestEntry, error_message);
    if (manifestBytes.isEmpty()) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
        set_error(error_message, QStringLiteral("UIL manifest is not valid JSON: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject manifest = manifestDocument.object();
    if (manifest.contains(QStringLiteral("format"))
        && manifest.value(QStringLiteral("format")).toString() != QStringLiteral("uil.presentation-package")) {
        set_error(error_message, QStringLiteral("Unsupported UIL package format"));
        return false;
    }

    if (manifest.contains(QStringLiteral("format_version"))
        && (manifest.value(QStringLiteral("format_version")).toInt() < 1
            || manifest.value(QStringLiteral("format_version")).toInt() > 2)) {
        set_error(error_message, QStringLiteral("Unsupported UIL package format version"));
        return false;
    }

    const QString entryPdf = normalized_package_path(
        manifest.value(QStringLiteral("entry_pdf")).toString(QStringLiteral("build/presentation.pdf")));
    if (!is_safe_relative_path(entryPdf)) {
        set_error(error_message, QStringLiteral("Unsafe entry_pdf path in UIL manifest"));
        return false;
    }

    if (!find_entry(entries, entryPdf)) {
        set_error(error_message, QStringLiteral("UIL package is missing entry PDF: %1").arg(entryPdf));
        return false;
    }

    QStringList movie_asset_paths;
    const QJsonArray movieAssets = manifest.value(QStringLiteral("movie_assets")).toArray();
    for (const QJsonValue& value : movieAssets) {
        if (!value.isObject()) {
            continue;
        }

        const QString rawPath = value.toObject().value(QStringLiteral("path")).toString();
        if (rawPath.isEmpty()) {
            continue;
        }
        const QString path = normalized_package_path(rawPath);
        if (!is_safe_relative_path(path)) {
            set_error(error_message, QStringLiteral("Unsafe movie asset path in UIL manifest: %1").arg(path));
            return false;
        }
        if (!find_entry(entries, path)) {
            set_error(error_message, QStringLiteral("UIL package is missing movie asset: %1").arg(path));
            return false;
        }
        if (!movie_asset_paths.contains(path)) {
            movie_asset_paths.push_back(path);
        }
    }

    QStringList molecule_asset_paths;
    const QJsonArray molecule_assets =
        manifest.value(QStringLiteral("molecule_assets")).toArray();
    for (const QJsonValue& value : molecule_assets) {
        if (!value.isObject()) {
            continue;
        }

        const QString raw_path = value.toObject().value(QStringLiteral("path")).toString();
        if (raw_path.isEmpty()) {
            continue;
        }
        const QString path = normalized_package_path(raw_path);
        if (!is_safe_relative_path(path)) {
            set_error(
                error_message,
                QStringLiteral("Unsafe molecule asset path in UIL manifest: %1").arg(path));
            return false;
        }
        if (!find_entry(entries, path)) {
            set_error(
                error_message,
                QStringLiteral("UIL package is missing molecule asset: %1").arg(path));
            return false;
        }
        if (!molecule_asset_paths.contains(path)) {
            molecule_asset_paths.push_back(path);
        }
    }

    QHash<int, QString> overlay_image_paths;
    const QJsonArray overlays = manifest.value(QStringLiteral("overlays")).toArray();
    for (const QJsonValue& value : overlays) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject overlayObject = value.toObject();
        const int page_index = overlayObject.value(QStringLiteral("page")).toInt(-1);
        const QString rawPath = overlayObject.value(QStringLiteral("path")).toString();
        if (page_index < 0 || rawPath.isEmpty()) {
            continue;
        }

        const QString path = normalized_package_path(rawPath);
        if (!is_safe_relative_path(path)) {
            set_error(error_message, QStringLiteral("Unsafe overlay path in UIL manifest: %1").arg(path));
            return false;
        }
        if (!find_entry(entries, path)) {
            set_error(error_message, QStringLiteral("UIL package is missing overlay image: %1").arg(path));
            return false;
        }
        overlay_image_paths.insert(page_index, path);
    }

    QSet<int> hidden_overlay_pages;
    const QJsonArray hiddenPages = manifest.value(QStringLiteral("hidden_overlay_pages")).toArray();
    for (const QJsonValue& value : hiddenPages) {
        const int page_index = value.toInt(-1);
        if (page_index >= 0) {
            hidden_overlay_pages.insert(page_index);
        }
    }

    QSet<QString> required_paths{
        QStringLiteral("manifest.json"),
        entryPdf
    };
    for (const QString& path : std::as_const(movie_asset_paths)) {
        required_paths.insert(path);
    }
    for (const QString& path : std::as_const(molecule_asset_paths)) {
        required_paths.insert(path);
    }
    for (const QString& path : std::as_const(overlay_image_paths)) {
        required_paths.insert(path);
    }

    for (const ZipEntry& entry : entries) {
        if (!required_paths.contains(entry.path)) {
            continue;
        }
        const QByteArray payload = entry_payload(bytes, entry, error_message);
        if (payload.isEmpty() && entry.uncompressedSize > 0) {
            return false;
        }

        const QString outputPath = destination.filePath(entry.path);
        if (!write_file(outputPath, payload, error_message)) {
            return false;
        }
    }

    result->entry_pdf_relative_path = entryPdf;
    result->entry_pdf_path = destination.filePath(entryPdf);
    result->package_root_path = destination.path();
    result->movie_asset_paths = movie_asset_paths;
    result->molecule_asset_paths = molecule_asset_paths;
    result->overlay_image_paths = overlay_image_paths;
    result->hidden_overlay_pages = hidden_overlay_pages;
    result->overlays_globally_visible = manifest.value(QStringLiteral("overlays_visible")).toBool(true);
    qCInfo(logUilPackage) << "Extracted UIL package" << package_path << "entry" << result->entry_pdf_path;
    return true;
}

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
    QString* error_message) {
    if (package_path.isEmpty()) {
        set_error(error_message, QStringLiteral("Missing UIL package path"));
        return false;
    }
    if (source_pdf_path.isEmpty()) {
        set_error(error_message, QStringLiteral("Missing source PDF path"));
        return false;
    }

    const QString entryPdf = normalized_package_path(entry_pdf_relative_path.isEmpty()
            ? QStringLiteral("build/presentation.pdf")
            : entry_pdf_relative_path);
    if (!is_safe_relative_path(entryPdf)) {
        set_error(error_message, QStringLiteral("Unsafe entry PDF path: %1").arg(entryPdf));
        return false;
    }

    QVector<ZipWriteEntry> entries;
    if (!add_file_zip_entry(&entries, entryPdf, source_pdf_path, error_message)) {
        return false;
    }

    QJsonArray movieAssets;
    for (const QString& rawPath : movie_asset_paths) {
        const QString assetPath = normalized_package_path(rawPath);
        if (!is_safe_relative_path(assetPath)) {
            set_error(error_message, QStringLiteral("Unsafe movie asset path: %1").arg(assetPath));
            return false;
        }

        const QString sourceAssetPath = QFileInfo(QDir(asset_root_path), assetPath).absoluteFilePath();
        if (!add_file_zip_entry(&entries, assetPath, sourceAssetPath, error_message)) {
            return false;
        }

        QJsonObject assetObject;
        assetObject.insert(QStringLiteral("path"), assetPath);
        movieAssets.append(assetObject);
    }

    QJsonArray molecule_assets;
    for (const QString& raw_path : molecule_asset_paths) {
        const QString asset_path = normalized_package_path(raw_path);
        if (!is_safe_relative_path(asset_path)) {
            set_error(
                error_message,
                QStringLiteral("Unsafe molecule asset path: %1").arg(asset_path));
            return false;
        }

        const QString source_asset_path =
            QFileInfo(QDir(asset_root_path), asset_path).absoluteFilePath();
        if (!add_file_zip_entry(&entries, asset_path, source_asset_path, error_message)) {
            return false;
        }

        QJsonObject asset_object;
        asset_object.insert(QStringLiteral("path"), asset_path);
        molecule_assets.append(asset_object);
    }

    QJsonArray overlays;
    QList<int> overlayPages = overlay_images.keys();
    std::sort(overlayPages.begin(), overlayPages.end());
    for (int page_index : overlayPages) {
        const QImage image = overlay_images.value(page_index);
        if (page_index < 0 || image.isNull()) {
            continue;
        }

        const QString overlayPath = QStringLiteral("overlays/page-%1.png").arg(page_index + 1, 4, 10, QLatin1Char('0'));
        const QByteArray overlayBytes = png_bytes_for_image(image, error_message);
        if (overlayBytes.isEmpty()) {
            return false;
        }
        if (!add_zip_entry(&entries, overlayPath, overlayBytes, error_message)) {
            return false;
        }

        QJsonObject overlayObject;
        overlayObject.insert(QStringLiteral("page"), page_index);
        overlayObject.insert(QStringLiteral("path"), overlayPath);
        overlays.append(overlayObject);
    }

    QJsonArray hiddenPages;
    QList<int> hiddenPageList = hidden_overlay_pages.values();
    std::sort(hiddenPageList.begin(), hiddenPageList.end());
    for (int page_index : hiddenPageList) {
        if (page_index >= 0) {
            hiddenPages.append(page_index);
        }
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("uil.presentation-package"));
    manifest.insert(QStringLiteral("format_version"), 2);
    manifest.insert(QStringLiteral("entry_pdf"), entryPdf);
    manifest.insert(QStringLiteral("movie_assets"), movieAssets);
    manifest.insert(QStringLiteral("molecule_assets"), molecule_assets);
    manifest.insert(QStringLiteral("overlays"), overlays);
    manifest.insert(QStringLiteral("hidden_overlay_pages"), hiddenPages);
    manifest.insert(QStringLiteral("overlays_visible"), overlays_globally_visible);

    if (!add_zip_entry(&entries, QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson(QJsonDocument::Indented), error_message)) {
        return false;
    }

    if (entries.size() > std::numeric_limits<quint16>::max()) {
        set_error(error_message, QStringLiteral("Too many files for ZIP32 package"));
        return false;
    }

    return write_zip_file(package_path, entries, error_message);
}
