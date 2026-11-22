#include "package/uil_package.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <limits>
#include <utility>

#include <zlib.h>

namespace {
/** @brief Writes test bytes to a file and creates its parent directory. */
bool write_test_file(const QString& path, const QByteArray& contents) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

/** @brief Writes a little-endian 32-bit value into mutable test bytes. */
void write_uint32(QByteArray* bytes, qsizetype offset, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) {
        (*bytes)[offset + byte] = char((value >> (byte * 8)) & 0xff);
    }
}

/** @brief Creates a valid package containing a PDF and one movie asset. */
QString create_test_package(QTemporaryDir* directory, QString* error_message) {
    const QString pdf_path = directory->filePath(QStringLiteral("deck.pdf"));
    const QString movie_path = directory->filePath(QStringLiteral("movies/clip.mov"));
    const QString package_path = directory->filePath(QStringLiteral("deck.uil"));
    if (!write_test_file(pdf_path, QByteArrayLiteral("%PDF-1.4\ntest\n%%EOF\n"))
        || !write_test_file(movie_path, QByteArrayLiteral("movie"))
        || !write_uil_package(
            package_path,
            pdf_path,
            QStringLiteral("slides/deck.pdf"),
            directory->path(),
            {QStringLiteral("movies/clip.mov")},
            {},
            {},
            {},
            true,
            error_message)) {
        return {};
    }
    return package_path;
}

/** @brief Reads all bytes from a test file. */
QByteArray read_test_file(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

/** @brief Appends a little-endian integer of @p byte_count bytes to test bytes. */
void append_le(QByteArray* bytes, quint32 value, int byte_count) {
    for (int byte = 0; byte < byte_count; ++byte) {
        bytes->append(char((value >> (byte * 8)) & 0xff));
    }
}

/**
 * @brief Builds a minimal stored ZIP archive so tests can supply hand-written manifests.
 * @param files Package-relative names and contents, written in order.
 */
QByteArray stored_zip(const QList<std::pair<QByteArray, QByteArray>>& files) {
    QByteArray archive;
    QByteArray central_directory;
    for (const auto& [name, data] : files) {
        const quint32 checksum = quint32(crc32(
            0L,
            reinterpret_cast<const Bytef*>(data.constData()),
            uInt(data.size())));
        const quint32 local_offset = quint32(archive.size());
        append_le(&archive, 0x04034b50, 4);
        append_le(&archive, 20, 2);
        append_le(&archive, 0, 2);
        append_le(&archive, 0, 2);
        append_le(&archive, 0, 4);
        append_le(&archive, checksum, 4);
        append_le(&archive, quint32(data.size()), 4);
        append_le(&archive, quint32(data.size()), 4);
        append_le(&archive, quint32(name.size()), 2);
        append_le(&archive, 0, 2);
        archive.append(name);
        archive.append(data);

        append_le(&central_directory, 0x02014b50, 4);
        append_le(&central_directory, 20, 2);
        append_le(&central_directory, 20, 2);
        append_le(&central_directory, 0, 2);
        append_le(&central_directory, 0, 2);
        append_le(&central_directory, 0, 4);
        append_le(&central_directory, checksum, 4);
        append_le(&central_directory, quint32(data.size()), 4);
        append_le(&central_directory, quint32(data.size()), 4);
        append_le(&central_directory, quint32(name.size()), 2);
        append_le(&central_directory, 0, 2);
        append_le(&central_directory, 0, 2);
        append_le(&central_directory, 0, 2);
        append_le(&central_directory, 0, 2);
        append_le(&central_directory, 0, 4);
        append_le(&central_directory, local_offset, 4);
        central_directory.append(name);
    }

    const quint32 central_directory_offset = quint32(archive.size());
    archive.append(central_directory);
    append_le(&archive, 0x06054b50, 4);
    append_le(&archive, 0, 2);
    append_le(&archive, 0, 2);
    append_le(&archive, quint32(files.size()), 2);
    append_le(&archive, quint32(files.size()), 2);
    append_le(&archive, quint32(central_directory.size()), 4);
    append_le(&archive, central_directory_offset, 4);
    append_le(&archive, 0, 2);
    return archive;
}
}  // namespace

class UilPackageTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies lossless round-trip packaging of documents, assets, and overlays. */
    void round_trip_preserves_package_contents();

    /** @brief Verifies that traversal through the entry-PDF path is rejected. */
    void unsafe_entry_path_is_rejected();

    /** @brief Verifies that duplicate package asset paths are rejected. */
    void duplicate_asset_path_is_rejected();

    /** @brief Verifies case-insensitive output-path collisions are rejected. */
    void case_insensitive_output_collision_is_rejected();

    /** @brief Verifies that missing source files produce actionable errors. */
    void missing_source_file_is_rejected();

    /** @brief Verifies that malformed archives fail without partial success. */
    void malformed_archive_is_rejected();

    /** @brief Verifies that corrupted entry data is rejected by its CRC. */
    void checksum_mismatch_is_rejected();

    /** @brief Verifies that Windows-style traversal is rejected while reading. */
    void backslash_traversal_is_rejected();

    /** @brief Verifies that duplicate normalized input paths are rejected. */
    void duplicate_input_path_is_rejected();

    /** @brief Verifies that declared decompression sizes are bounded. */
    void oversized_entry_is_rejected();

    /** @brief Verifies that callers must provide an extraction result. */
    void missing_result_pointer_is_rejected();

    /** @brief Verifies that manifest page indices above the supported cap are ignored. */
    void out_of_range_manifest_page_indices_are_ignored();

    /** @brief Verifies that writing skips overlay pages whose file number would overflow. */
    void out_of_range_overlay_pages_are_not_written();
};

void UilPackageTest::round_trip_preserves_package_contents() {
    QTemporaryDir source_directory;
    QTemporaryDir extraction_directory;
    QVERIFY(source_directory.isValid());
    QVERIFY(extraction_directory.isValid());

    const QByteArray pdf_contents("%PDF-1.4\nunit-test-deck\n%%EOF\n");
    const QByteArray movie_contents("synthetic movie payload");
    const QByteArray molecule_contents("1\nHydrogen\nH 0 0 0\n");
    const QString pdf_path = source_directory.filePath(QStringLiteral("deck.pdf"));
    const QString movie_relative_path = QStringLiteral("movies/clip.mov");
    const QString movie_path = source_directory.filePath(movie_relative_path);
    const QString molecule_relative_path = QStringLiteral("molecules/hydrogen.xyz");
    const QString molecule_path = source_directory.filePath(molecule_relative_path);
    const QString package_path = source_directory.filePath(QStringLiteral("output/deck.uil"));
    QVERIFY(write_test_file(pdf_path, pdf_contents));
    QVERIFY(write_test_file(movie_path, movie_contents));
    QVERIFY(write_test_file(molecule_path, molecule_contents));

    QImage overlay(3, 2, QImage::Format_ARGB32);
    overlay.fill(qRgba(20, 40, 60, 128));
    const QHash<int, QImage> overlays{{2, overlay}, {-1, overlay}, {4, QImage()}};
    const QSet<int> hidden_pages{2, -1};
    QString error_message;

    QVERIFY2(write_uil_package(package_path,
                              pdf_path,
                              QStringLiteral("slides/deck.pdf"),
                              source_directory.path(),
                              {movie_relative_path},
                              {molecule_relative_path},
                              overlays,
                              hidden_pages,
                              false,
                              &error_message),
             qPrintable(error_message));

    UilPackageOpenResult result;
    QVERIFY2(extract_uil_package(package_path, extraction_directory, &result, &error_message),
             qPrintable(error_message));
    QCOMPARE(result.entry_pdf_relative_path, QStringLiteral("slides/deck.pdf"));
    QCOMPARE(read_test_file(result.entry_pdf_path), pdf_contents);
    QCOMPARE(result.movie_asset_paths, QStringList{movie_relative_path});
    QCOMPARE(result.molecule_asset_paths, QStringList{molecule_relative_path});
    QCOMPARE(read_test_file(extraction_directory.filePath(movie_relative_path)), movie_contents);
    QCOMPARE(
        read_test_file(extraction_directory.filePath(molecule_relative_path)),
        molecule_contents);
    QVERIFY(result.hidden_overlay_pages == QSet<int>{2});
    QVERIFY(!result.overlays_globally_visible);
    QCOMPARE(result.overlay_image_paths.size(), 1);
    QVERIFY(result.overlay_image_paths.contains(2));

    const QImage extracted_overlay(
        extraction_directory.filePath(result.overlay_image_paths.value(2)));
    QCOMPARE(extracted_overlay.size(), overlay.size());
    QCOMPARE(extracted_overlay.pixelColor(1, 1), overlay.pixelColor(1, 1));
}

void UilPackageTest::unsafe_entry_path_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_test_file(source_path, QByteArrayLiteral("pdf")));
    QString error_message;

    QVERIFY(!write_uil_package(directory.filePath(QStringLiteral("deck.uil")),
                              source_path,
                              QStringLiteral("../outside.pdf"),
                              directory.path(),
                              {},
                              {},
                              {},
                              {},
                              true,
                              &error_message));
    QVERIFY(error_message.contains(QStringLiteral("Unsafe entry PDF path")));
}

void UilPackageTest::duplicate_asset_path_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source_path = directory.filePath(QStringLiteral("deck.pdf"));
    const QString asset_path = directory.filePath(QStringLiteral("movies/clip.mov"));
    QVERIFY(write_test_file(source_path, QByteArrayLiteral("pdf")));
    QVERIFY(write_test_file(asset_path, QByteArrayLiteral("movie")));
    QString error_message;

    QVERIFY(!write_uil_package(directory.filePath(QStringLiteral("deck.uil")),
                              source_path,
                              QStringLiteral("deck.pdf"),
                              directory.path(),
                              {QStringLiteral("movies/clip.mov"),
                               QStringLiteral("movies/clip.mov")},
                              {},
                              {},
                              {},
                              true,
                              &error_message));
    QVERIFY(error_message.contains(QStringLiteral("Duplicate package entry")));
}

void UilPackageTest::case_insensitive_output_collision_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source_path = directory.filePath(QStringLiteral("deck.pdf"));
    const QString colliding_asset_path = directory.filePath(QStringLiteral("slides/DECK.PDF"));
    QVERIFY(write_test_file(source_path, QByteArrayLiteral("pdf")));
    QVERIFY(write_test_file(colliding_asset_path, QByteArrayLiteral("asset")));
    QString error_message;

    QVERIFY(!write_uil_package(directory.filePath(QStringLiteral("deck.uil")),
                              source_path,
                              QStringLiteral("slides/deck.pdf"),
                              directory.path(),
                              {QStringLiteral("slides/DECK.PDF")},
                              {},
                              {},
                              {},
                              true,
                              &error_message));
    QVERIFY(error_message.contains(QStringLiteral("Duplicate package entry")));
}

void UilPackageTest::missing_source_file_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error_message;

    QVERIFY(!write_uil_package(directory.filePath(QStringLiteral("deck.uil")),
                              directory.filePath(QStringLiteral("missing.pdf")),
                              QStringLiteral("deck.pdf"),
                              directory.path(),
                              {},
                              {},
                              {},
                              {},
                              true,
                              &error_message));
    QVERIFY(error_message.contains(QStringLiteral("Could not read")));
}

void UilPackageTest::malformed_archive_is_rejected() {
    QTemporaryDir directory;
    QTemporaryDir extraction_directory;
    QVERIFY(directory.isValid());
    QVERIFY(extraction_directory.isValid());
    const QString package_path = directory.filePath(QStringLiteral("broken.uil"));
    QVERIFY(write_test_file(package_path, QByteArrayLiteral("not a ZIP archive")));
    QString error_message;
    UilPackageOpenResult result;

    QVERIFY(!extract_uil_package(package_path, extraction_directory, &result, &error_message));
    QVERIFY(!error_message.isEmpty());
    QVERIFY(result.entry_pdf_path.isEmpty());
}

void UilPackageTest::checksum_mismatch_is_rejected() {
    QTemporaryDir directory;
    QTemporaryDir extraction_directory;
    QVERIFY(directory.isValid());
    QVERIFY(extraction_directory.isValid());
    QString error_message;
    const QString package_path = create_test_package(&directory, &error_message);
    QVERIFY2(!package_path.isEmpty(), qPrintable(error_message));

    QByteArray bytes = read_test_file(package_path);
    const qsizetype central_header = bytes.indexOf(QByteArrayLiteral("PK\x01\x02"));
    QVERIFY(central_header >= 0);
    bytes[central_header + 16] ^= char(0x01);
    QVERIFY(write_test_file(package_path, bytes));

    UilPackageOpenResult result;
    QVERIFY(!extract_uil_package(package_path, extraction_directory, &result, &error_message));
    QVERIFY(error_message.contains(QStringLiteral("checksum mismatch"), Qt::CaseInsensitive));
}

void UilPackageTest::backslash_traversal_is_rejected() {
    QTemporaryDir directory;
    QTemporaryDir extraction_directory;
    QVERIFY(directory.isValid());
    QVERIFY(extraction_directory.isValid());
    QString error_message;
    const QString package_path = create_test_package(&directory, &error_message);
    QVERIFY2(!package_path.isEmpty(), qPrintable(error_message));

    QByteArray bytes = read_test_file(package_path);
    QCOMPARE(bytes.replace("slides/deck.pdf", "..\\outside.pdfx").count("..\\outside.pdfx"), 3);
    QVERIFY(write_test_file(package_path, bytes));

    UilPackageOpenResult result;
    QVERIFY(!extract_uil_package(package_path, extraction_directory, &result, &error_message));
    QVERIFY(error_message.contains(QStringLiteral("Unsafe path")));
}

void UilPackageTest::duplicate_input_path_is_rejected() {
    QTemporaryDir directory;
    QTemporaryDir extraction_directory;
    QVERIFY(directory.isValid());
    QVERIFY(extraction_directory.isValid());
    QString error_message;
    const QString package_path = create_test_package(&directory, &error_message);
    QVERIFY2(!package_path.isEmpty(), qPrintable(error_message));

    QByteArray bytes = read_test_file(package_path);
    QCOMPARE(bytes.replace("movies/clip.mov", "slides/deck.pdf").count("slides/deck.pdf"), 6);
    QVERIFY(write_test_file(package_path, bytes));

    UilPackageOpenResult result;
    QVERIFY(!extract_uil_package(package_path, extraction_directory, &result, &error_message));
    QVERIFY(error_message.contains(QStringLiteral("Duplicate path")));
}

void UilPackageTest::oversized_entry_is_rejected() {
    QTemporaryDir directory;
    QTemporaryDir extraction_directory;
    QVERIFY(directory.isValid());
    QVERIFY(extraction_directory.isValid());
    QString error_message;
    const QString package_path = create_test_package(&directory, &error_message);
    QVERIFY2(!package_path.isEmpty(), qPrintable(error_message));

    QByteArray bytes = read_test_file(package_path);
    const qsizetype central_header = bytes.indexOf(QByteArrayLiteral("PK\x01\x02"));
    QVERIFY(central_header >= 0);
    write_uint32(&bytes, central_header + 24, 768U * 1024U * 1024U);
    QVERIFY(write_test_file(package_path, bytes));

    UilPackageOpenResult result;
    QVERIFY(!extract_uil_package(package_path, extraction_directory, &result, &error_message));
    QVERIFY(error_message.contains(QStringLiteral("extraction limit")));
}

void UilPackageTest::missing_result_pointer_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error_message;

    QVERIFY(!extract_uil_package(QString(), directory, nullptr, &error_message));
    QVERIFY(error_message.contains(QStringLiteral("missing package result")));
}

void UilPackageTest::out_of_range_manifest_page_indices_are_ignored() {
    QTemporaryDir directory;
    QTemporaryDir extraction_directory;
    QVERIFY(directory.isValid());
    QVERIFY(extraction_directory.isValid());

    const int maximum_int = std::numeric_limits<int>::max();
    const QJsonArray overlays{
        QJsonObject{{QStringLiteral("page"), maximum_int},
                    {QStringLiteral("path"), QStringLiteral("overlays/huge.png")}},
        QJsonObject{{QStringLiteral("page"), kMaximumUilPackagePageIndex + 1},
                    {QStringLiteral("path"), QStringLiteral("overlays/over.png")}},
        QJsonObject{{QStringLiteral("page"), kMaximumUilPackagePageIndex},
                    {QStringLiteral("path"), QStringLiteral("overlays/last.png")}},
    };
    const QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("uil.presentation-package")},
        {QStringLiteral("format_version"), 2},
        {QStringLiteral("entry_pdf"), QStringLiteral("deck.pdf")},
        {QStringLiteral("overlays"), overlays},
        {QStringLiteral("hidden_overlay_pages"),
         QJsonArray{maximum_int, kMaximumUilPackagePageIndex + 1, 3}},
    };
    const QString package_path = directory.filePath(QStringLiteral("hostile.uil"));
    QVERIFY(write_test_file(package_path, stored_zip({
        {QByteArrayLiteral("manifest.json"), QJsonDocument(manifest).toJson()},
        {QByteArrayLiteral("deck.pdf"), QByteArrayLiteral("%PDF-1.4\n%%EOF\n")},
        {QByteArrayLiteral("overlays/huge.png"), QByteArrayLiteral("png")},
        {QByteArrayLiteral("overlays/over.png"), QByteArrayLiteral("png")},
        {QByteArrayLiteral("overlays/last.png"), QByteArrayLiteral("png")},
    })));

    UilPackageOpenResult result;
    QString error_message;
    QVERIFY2(extract_uil_package(package_path, extraction_directory, &result, &error_message),
             qPrintable(error_message));
    QCOMPARE(result.overlay_image_paths.size(), 1);
    QCOMPARE(result.overlay_image_paths.value(kMaximumUilPackagePageIndex),
             QStringLiteral("overlays/last.png"));
    QVERIFY(result.hidden_overlay_pages == QSet<int>{3});
    QVERIFY(!QFile::exists(extraction_directory.filePath(QStringLiteral("overlays/huge.png"))));
}

void UilPackageTest::out_of_range_overlay_pages_are_not_written() {
    QTemporaryDir source_directory;
    QTemporaryDir extraction_directory;
    QVERIFY(source_directory.isValid());
    QVERIFY(extraction_directory.isValid());
    const QString pdf_path = source_directory.filePath(QStringLiteral("deck.pdf"));
    const QString package_path = source_directory.filePath(QStringLiteral("deck.uil"));
    QVERIFY(write_test_file(pdf_path, QByteArrayLiteral("%PDF-1.4\n%%EOF\n")));

    QImage overlay(2, 2, QImage::Format_ARGB32);
    overlay.fill(qRgba(10, 20, 30, 255));
    const int maximum_int = std::numeric_limits<int>::max();
    const QHash<int, QImage> overlays{
        {maximum_int, overlay},
        {kMaximumUilPackagePageIndex + 1, overlay},
        {kMaximumUilPackagePageIndex, overlay},
    };
    QString error_message;
    QVERIFY2(write_uil_package(package_path,
                               pdf_path,
                               QString(),
                               source_directory.path(),
                               {},
                               {},
                               overlays,
                               {maximum_int, 1},
                               true,
                               &error_message),
             qPrintable(error_message));

    UilPackageOpenResult result;
    QVERIFY2(extract_uil_package(package_path, extraction_directory, &result, &error_message),
             qPrintable(error_message));
    QCOMPARE(result.entry_pdf_relative_path, QStringLiteral("build/presentation.pdf"));
    QCOMPARE(result.overlay_image_paths.size(), 1);
    QCOMPARE(result.overlay_image_paths.value(kMaximumUilPackagePageIndex),
             QStringLiteral("overlays/page-100000.png"));
    QVERIFY(result.hidden_overlay_pages == QSet<int>{1});
}

QTEST_GUILESS_MAIN(UilPackageTest)

#include "uil_package_test.moc"
