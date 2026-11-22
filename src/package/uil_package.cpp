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

Q_LOGGING_CATEGORY(log_uil_package, "uil.package")

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

/** @brief Returns whether a manifest page index is within the supported range. */
bool is_supported_page_index(int page_index) {
    return page_index >= 0 && page_index <= kMaximumUilPackagePageIndex;
}

/** @brief Central-directory metadata for one file entry of a package being read. */
struct ZipEntry {
    QString path;
    quint16 compression_method = 0;
    quint32 crc = 0;
    quint32 compressed_size = 0;
    quint32 uncompressed_size = 0;
    quint32 local_header_offset = 0;
};

/** @brief Normalizes separators and removes redundant path components. */
QString normalized_package_path(const QString& path) {
    QString portable_path = path;
    portable_path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(portable_path);
}

/** @brief One file queued for writing into a stored ZIP archive. */
struct ZipWriteEntry {
    QString path;
    QByteArray data;
    quint32 crc = 0;
    quint32 local_header_offset = 0;
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

    const qsizetype min_offset = std::max<qsizetype>(0, bytes.size() - 65557);
    for (qsizetype offset = bytes.size() - 22; offset >= min_offset; --offset) {
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
    const std::optional<qsizetype> eocd_offset = find_end_of_central_directory(bytes);
    if (!eocd_offset) {
        set_error(error_message, QStringLiteral("Missing ZIP end-of-central-directory record"));
        return false;
    }

    const quint16 disk_number = read_uint16(bytes, *eocd_offset + 4);
    const quint16 central_directory_disk = read_uint16(bytes, *eocd_offset + 6);
    const quint16 disk_entry_count = read_uint16(bytes, *eocd_offset + 8);
    const quint16 entry_count = read_uint16(bytes, *eocd_offset + 10);
    const quint32 central_directory_size = read_uint32(bytes, *eocd_offset + 12);
    const quint32 central_directory_offset = read_uint32(bytes, *eocd_offset + 16);
    const quint16 comment_length = read_uint16(bytes, *eocd_offset + 20);
    if (disk_number != 0 || central_directory_disk != 0 || disk_entry_count != entry_count) {
        set_error(error_message, QStringLiteral("Multi-disk ZIP packages are not supported"));
        return false;
    }

    if (*eocd_offset + 22 + comment_length != bytes.size()) {
        set_error(error_message, QStringLiteral("ZIP end record has an invalid comment or trailing data"));
        return false;
    }

    if (central_directory_offset == 0xffffffffu || central_directory_size == 0xffffffffu || entry_count == 0xffffu) {
        set_error(error_message, QStringLiteral("ZIP64 packages are not supported"));
        return false;
    }

    if (entry_count > kMaximumEntryCount) {
        set_error(error_message, QStringLiteral("UIL package contains too many files"));
        return false;
    }

    if (qsizetype(central_directory_offset) + qsizetype(central_directory_size) > bytes.size()) {
        set_error(error_message, QStringLiteral("ZIP central directory points outside the package"));
        return false;
    }

    entries->clear();
    QSet<QString> normalized_paths;
    quint64 total_uncompressed_size = 0;
    qsizetype offset = central_directory_offset;
    const qsizetype central_directory_end = qsizetype(central_directory_offset) + qsizetype(central_directory_size);
    for (quint16 i = 0; i < entry_count; ++i) {
        if (offset + 46 > central_directory_end || read_uint32(bytes, offset) != kCentralDirectoryHeaderSignature) {
            set_error(error_message, QStringLiteral("Invalid ZIP central directory entry"));
            return false;
        }

        const quint16 general_purpose_flags = read_uint16(bytes, offset + 8);
        const quint16 compression_method = read_uint16(bytes, offset + 10);
        const quint32 checksum = read_uint32(bytes, offset + 16);
        const quint32 compressed_size = read_uint32(bytes, offset + 20);
        const quint32 uncompressed_size = read_uint32(bytes, offset + 24);
        const quint16 file_name_length = read_uint16(bytes, offset + 28);
        const quint16 extra_length = read_uint16(bytes, offset + 30);
        const quint16 entry_comment_length = read_uint16(bytes, offset + 32);
        const quint32 local_header_offset = read_uint32(bytes, offset + 42);
        const qsizetype name_offset = offset + 46;
        const qsizetype next_offset = name_offset + file_name_length + extra_length + entry_comment_length;
        if (next_offset > central_directory_end) {
            set_error(error_message, QStringLiteral("Invalid ZIP entry name"));
            return false;
        }

        if (general_purpose_flags & 0x0001) {
            set_error(error_message, QStringLiteral("Encrypted ZIP entries are not supported"));
            return false;
        }

        if (compression_method != kZipStoreMethod && compression_method != kZipDeflateMethod) {
            set_error(error_message, QStringLiteral("Unsupported ZIP compression method"));
            return false;
        }

        if (uncompressed_size > kMaximumEntryBytes) {
            set_error(error_message, QStringLiteral("UIL package entry exceeds the extraction limit"));
            return false;
        }
        total_uncompressed_size += uncompressed_size;
        if (total_uncompressed_size > kMaximumExtractedBytes) {
            set_error(error_message, QStringLiteral("UIL package exceeds the total extraction limit"));
            return false;
        }
        if (compression_method == kZipDeflateMethod
            && uncompressed_size > 1024 * 1024
            && (compressed_size == 0
                || quint64(uncompressed_size) > quint64(compressed_size) * kMaximumCompressionRatio)) {
            set_error(error_message, QStringLiteral("UIL package entry has an unsafe compression ratio"));
            return false;
        }

        const QByteArray encoded_path = bytes.mid(name_offset, file_name_length);
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
                compression_method,
                checksum,
                compressed_size,
                uncompressed_size,
                local_header_offset});
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
    const qsizetype offset = entry.local_header_offset;
    if (offset + 30 > bytes.size() || read_uint32(bytes, offset) != kLocalFileHeaderSignature) {
        set_error(error_message, QStringLiteral("Invalid local ZIP header for %1").arg(entry.path));
        return {};
    }

    const quint16 file_name_length = read_uint16(bytes, offset + 26);
    const quint16 extra_length = read_uint16(bytes, offset + 28);
    const quint16 local_compression_method = read_uint16(bytes, offset + 8);
    if (local_compression_method != entry.compression_method) {
        set_error(error_message, QStringLiteral("ZIP compression method mismatch for %1").arg(entry.path));
        return {};
    }
    const QByteArray local_encoded_path = bytes.mid(offset + 30, file_name_length);
    const QString local_decoded_path = QString::fromUtf8(local_encoded_path);
    const QString local_path = normalized_package_path(local_decoded_path);
    if (local_path != entry.path || local_decoded_path.toUtf8() != local_encoded_path) {
        set_error(error_message, QStringLiteral("ZIP local and central names differ for %1").arg(entry.path));
        return {};
    }
    const qsizetype data_offset = offset + 30 + file_name_length + extra_length;
    if (data_offset + entry.compressed_size > bytes.size()) {
        set_error(error_message, QStringLiteral("ZIP entry payload points outside the package: %1").arg(entry.path));
        return {};
    }

    const QByteArray compressed = bytes.mid(data_offset, entry.compressed_size);
    if (entry.compression_method == kZipStoreMethod) {
        if (compressed.size() != int(entry.uncompressed_size)) {
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

    if (entry.compression_method == kZipDeflateMethod) {
        QByteArray inflated = inflate_raw_deflate(compressed, entry.uncompressed_size, error_message);
        if (inflated.size() != int(entry.uncompressed_size)) {
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

    set_error(error_message, QStringLiteral("Unsupported ZIP compression method %1 for %2").arg(entry.compression_method).arg(entry.path));
    return {};
}

/** @brief Finds a ZIP entry by normalized package path. */
const ZipEntry* find_entry(const QVector<ZipEntry>& entries, const QString& path) {
    const QString clean_path = normalized_package_path(path);
    for (const ZipEntry& entry : entries) {
        if (entry.path == clean_path) {
            return &entry;
        }
    }

    return nullptr;
}

/** @brief Writes bytes to a file, creating its parent directory as needed. */
bool write_file(const QString& path, const QByteArray& contents, QString* error_message) {
    const QFileInfo file_info(path);
    QDir dir;
    if (!dir.mkpath(file_info.absolutePath())) {
        set_error(error_message, QStringLiteral("Could not create directory: %1").arg(file_info.absolutePath()));
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
    const QString clean_path = normalized_package_path(path);
    if (!is_safe_relative_path(clean_path)) {
        set_error(error_message, QStringLiteral("Unsafe package output path: %1").arg(path));
        return false;
    }
    if (quint64(data.size()) > kMaximumEntryBytes) {
        set_error(error_message, QStringLiteral("Package entry is too large: %1").arg(clean_path));
        return false;
    }
    if (entries->size() >= kMaximumEntryCount) {
        set_error(error_message, QStringLiteral("UIL package contains too many files"));
        return false;
    }

    const auto existing = std::find_if(entries->cbegin(), entries->cend(), [&clean_path](const ZipWriteEntry& entry) {
        return entry.path.compare(clean_path, Qt::CaseInsensitive) == 0;
    });
    if (existing != entries->cend()) {
        set_error(error_message, QStringLiteral("Duplicate package entry: %1").arg(clean_path));
        return false;
    }

    const quint32 checksum = crc32(0L, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size()));
    entries->push_back(ZipWriteEntry{clean_path, data, checksum, 0});
    return true;
}

/** @brief Adds a filesystem file to a pending ZIP archive. */
bool add_file_zip_entry(QVector<ZipWriteEntry>* entries, const QString& package_path, const QString& file_path, QString* error_message) {
    QByteArray data;
    if (!read_file_bytes(file_path, &data, error_message)) {
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

        entry.local_header_offset = quint32(output.size());
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

    const quint32 central_directory_offset = quint32(output.size());
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
        append_uint32(&output, entry.local_header_offset);
        output.append(name);
    }

    const quint32 central_directory_size = quint32(output.size()) - central_directory_offset;
    append_uint32(&output, kEndOfCentralDirectorySignature);
    append_uint16(&output, 0);
    append_uint16(&output, 0);
    append_uint16(&output, quint16(entries.size()));
    append_uint16(&output, quint16(entries.size()));
    append_uint32(&output, central_directory_size);
    append_uint32(&output, central_directory_offset);
    append_uint16(&output, 0);

    const QFileInfo output_info(path);
    QDir dir;
    if (!dir.mkpath(output_info.absolutePath())) {
        set_error(error_message, QStringLiteral("Could not create directory: %1").arg(output_info.absolutePath()));
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
}  // namespace

bool extract_uil_package(const QString& package_path, QTemporaryDir& destination, UilPackageOpenResult* result, QString* error_message) {
    if (!result) {
        set_error(error_message, QStringLiteral("Internal error: missing package result"));
        return false;
    }

    QFile package_file(package_path);
    if (!package_file.open(QIODevice::ReadOnly)) {
        set_error(error_message, QStringLiteral("Could not open UIL package: %1").arg(package_file.errorString()));
        return false;
    }

    if (quint64(package_file.size()) > kMaximumPackageBytes) {
        set_error(error_message, QStringLiteral("UIL package exceeds the maximum supported size"));
        return false;
    }

    const QByteArray bytes = package_file.readAll();
    if (package_file.error() != QFileDevice::NoError || bytes.size() != package_file.size()) {
        set_error(error_message, QStringLiteral("Could not completely read UIL package: %1").arg(package_file.errorString()));
        return false;
    }
    QVector<ZipEntry> entries;
    if (!parse_central_directory(bytes, &entries, error_message)) {
        return false;
    }

    const ZipEntry* manifest_entry = find_entry(entries, QStringLiteral("manifest.json"));
    if (!manifest_entry) {
        set_error(error_message, QStringLiteral("UIL package is missing manifest.json"));
        return false;
    }
    if (manifest_entry->uncompressed_size > kMaximumManifestBytes) {
        set_error(error_message, QStringLiteral("UIL manifest exceeds the size limit"));
        return false;
    }

    const QByteArray manifest_bytes = entry_payload(bytes, *manifest_entry, error_message);
    if (manifest_bytes.isEmpty()) {
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument manifest_document = QJsonDocument::fromJson(manifest_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !manifest_document.isObject()) {
        set_error(error_message, QStringLiteral("UIL manifest is not valid JSON: %1").arg(parse_error.errorString()));
        return false;
    }

    const QJsonObject manifest = manifest_document.object();
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

    const QString entry_pdf = normalized_package_path(
        manifest.value(QStringLiteral("entry_pdf")).toString(QStringLiteral("build/presentation.pdf")));
    if (!is_safe_relative_path(entry_pdf)) {
        set_error(error_message, QStringLiteral("Unsafe entry_pdf path in UIL manifest"));
        return false;
    }

    if (!find_entry(entries, entry_pdf)) {
        set_error(error_message, QStringLiteral("UIL package is missing entry PDF: %1").arg(entry_pdf));
        return false;
    }

    QStringList movie_asset_paths;
    const QJsonArray movie_assets = manifest.value(QStringLiteral("movie_assets")).toArray();
    for (const QJsonValue& value : movie_assets) {
        if (!value.isObject()) {
            continue;
        }

        const QString raw_path = value.toObject().value(QStringLiteral("path")).toString();
        if (raw_path.isEmpty()) {
            continue;
        }
        const QString path = normalized_package_path(raw_path);
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

        const QJsonObject overlay_object = value.toObject();
        const int page_index = overlay_object.value(QStringLiteral("page")).toInt(-1);
        const QString raw_path = overlay_object.value(QStringLiteral("path")).toString();
        if (!is_supported_page_index(page_index) || raw_path.isEmpty()) {
            if (page_index > kMaximumUilPackagePageIndex) {
                qCWarning(log_uil_package)
                    << "Ignoring overlay for out-of-range page" << page_index;
            }
            continue;
        }

        const QString path = normalized_package_path(raw_path);
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
    const QJsonArray hidden_pages = manifest.value(QStringLiteral("hidden_overlay_pages")).toArray();
    for (const QJsonValue& value : hidden_pages) {
        const int page_index = value.toInt(-1);
        if (is_supported_page_index(page_index)) {
            hidden_overlay_pages.insert(page_index);
        }
    }

    QSet<QString> required_paths{
        QStringLiteral("manifest.json"),
        entry_pdf
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
        if (payload.isEmpty() && entry.uncompressed_size > 0) {
            return false;
        }

        const QString output_path = destination.filePath(entry.path);
        if (!write_file(output_path, payload, error_message)) {
            return false;
        }
    }

    result->entry_pdf_relative_path = entry_pdf;
    result->entry_pdf_path = destination.filePath(entry_pdf);
    result->package_root_path = destination.path();
    result->movie_asset_paths = movie_asset_paths;
    result->molecule_asset_paths = molecule_asset_paths;
    result->overlay_image_paths = overlay_image_paths;
    result->hidden_overlay_pages = hidden_overlay_pages;
    result->overlays_globally_visible = manifest.value(QStringLiteral("overlays_visible")).toBool(true);
    qCInfo(log_uil_package) << "Extracted UIL package" << package_path << "entry" << result->entry_pdf_path;
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

    const QString entry_pdf = normalized_package_path(entry_pdf_relative_path.isEmpty()
            ? QStringLiteral("build/presentation.pdf")
            : entry_pdf_relative_path);
    if (!is_safe_relative_path(entry_pdf)) {
        set_error(error_message, QStringLiteral("Unsafe entry PDF path: %1").arg(entry_pdf));
        return false;
    }

    QVector<ZipWriteEntry> entries;
    if (!add_file_zip_entry(&entries, entry_pdf, source_pdf_path, error_message)) {
        return false;
    }

    QJsonArray movie_assets;
    for (const QString& raw_path : movie_asset_paths) {
        const QString asset_path = normalized_package_path(raw_path);
        if (!is_safe_relative_path(asset_path)) {
            set_error(error_message, QStringLiteral("Unsafe movie asset path: %1").arg(asset_path));
            return false;
        }

        const QString source_asset_path = QFileInfo(QDir(asset_root_path), asset_path).absoluteFilePath();
        if (!add_file_zip_entry(&entries, asset_path, source_asset_path, error_message)) {
            return false;
        }

        QJsonObject asset_object;
        asset_object.insert(QStringLiteral("path"), asset_path);
        movie_assets.append(asset_object);
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
    QList<int> overlay_pages = overlay_images.keys();
    std::sort(overlay_pages.begin(), overlay_pages.end());
    for (int page_index : overlay_pages) {
        const QImage image = overlay_images.value(page_index);
        if (!is_supported_page_index(page_index) || image.isNull()) {
            continue;
        }

        // The supported-index check above keeps the one-based file number from overflowing.
        const QString overlay_path = QStringLiteral("overlays/page-%1.png").arg(page_index + 1, 4, 10, QLatin1Char('0'));
        const QByteArray overlay_bytes = png_bytes_for_image(image, error_message);
        if (overlay_bytes.isEmpty()) {
            return false;
        }
        if (!add_zip_entry(&entries, overlay_path, overlay_bytes, error_message)) {
            return false;
        }

        QJsonObject overlay_object;
        overlay_object.insert(QStringLiteral("page"), page_index);
        overlay_object.insert(QStringLiteral("path"), overlay_path);
        overlays.append(overlay_object);
    }

    QJsonArray hidden_pages;
    QList<int> hidden_page_list = hidden_overlay_pages.values();
    std::sort(hidden_page_list.begin(), hidden_page_list.end());
    for (int page_index : hidden_page_list) {
        if (is_supported_page_index(page_index)) {
            hidden_pages.append(page_index);
        }
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("uil.presentation-package"));
    manifest.insert(QStringLiteral("format_version"), 2);
    manifest.insert(QStringLiteral("entry_pdf"), entry_pdf);
    manifest.insert(QStringLiteral("movie_assets"), movie_assets);
    manifest.insert(QStringLiteral("molecule_assets"), molecule_assets);
    manifest.insert(QStringLiteral("overlays"), overlays);
    manifest.insert(QStringLiteral("hidden_overlay_pages"), hidden_pages);
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
