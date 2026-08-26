#include "util/pdf_media_detector.hpp"

#include "media/video_frame_extractor.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace {
/** @brief Writes a synthetic PDF fixture to disk. */
bool write_pdf_fixture(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

/** @brief Returns a minimal two-page PDF source containing linked and orphan media annotations. */
QByteArray media_pdf_fixture() {
    return QByteArrayLiteral(
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /Annots [5 0 R] >>\nendobj\n"
        "4 0 obj\n<< /Type /Page /Parent 2 0 R >>\nendobj\n"
        "5 0 obj\n<< /Type /Annot /Subtype /Movie /Rect [30 50 10 20] "
        "/Movie << /F (movies/clip.mov) >> >>\nendobj\n"
        "6 0 obj\n<< /Type /Annot /Subtype /Sound /Rect [1 2 3 4] "
        "/F (orphan.wav) >>\nendobj\n"
        "%%EOF\n");
}

/** @brief Returns a one-page PDF fixture referencing a caller-provided movie path. */
QByteArray movie_path_pdf_fixture(const QByteArray& movie_path) {
    return QByteArrayLiteral(
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>\nendobj\n"
        "4 0 obj\n<< /Type /Annot /Subtype /Movie /Rect [0 0 10 10] /F (")
        + movie_path
        + QByteArrayLiteral(") >>\nendobj\n%%EOF\n");
}
}

class PdfMediaDetectorTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies annotation media-type and preview-frame helpers. */
    void annotation_helpers_report_state();

    /** @brief Verifies the empty-scan summary contract. */
    void empty_result_has_clear_summary();

    /** @brief Verifies page ordering, metadata parsing, path resolution, and orphan detection. */
    void scans_linked_and_orphan_annotations();

    /** @brief Verifies that nonexistent PDFs produce an empty result. */
    void missing_pdf_returns_empty_result();

    /** @brief Verifies that raw PDFs cannot escape their containing directory. */
    void traversal_media_path_is_rejected();

    /** @brief Verifies that packages can only resolve declared media assets. */
    void undeclared_package_media_is_rejected();

#if defined(UIL_HAVE_FFMPEG)
    /** @brief Verifies that the optional FFmpeg runtime can be loaded on first use. */
    void ffmpeg_runtime_loads_lazily();
#endif
};

void PdfMediaDetectorTest::annotation_helpers_report_state() {
    PdfMediaAnnotation annotation;
    QVERIFY(!annotation.has_first_frame());
    QVERIFY(!annotation.is_mp4());

    annotation.fileName = QStringLiteral("CLIP.MP4");
    QVERIFY(annotation.is_mp4());

    annotation.fileName.clear();
    annotation.resolved_file_path = QStringLiteral("C:/media/clip.mp4");
    QVERIFY(annotation.is_mp4());

    annotation.first_frame = QImage(1, 1, QImage::Format_ARGB32);
    QVERIFY(annotation.has_first_frame());
}

void PdfMediaDetectorTest::empty_result_has_clear_summary() {
    const PdfMediaScanResult result;
    QVERIFY(!result.has_media());
    QCOMPARE(result.summary(), QStringLiteral("No PDF media annotations detected"));
}

void PdfMediaDetectorTest::scans_linked_and_orphan_annotations() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, media_pdf_fixture()));
    const QString package_root = directory.filePath(QStringLiteral("package"));

    const PdfMediaScanResult result = scan_pdf_media_annotations(
        pdf_path, package_root, {QStringLiteral("movies/clip.mov")});
    QVERIFY(result.has_media());
    QCOMPARE(result.annotations.size(), 2);

    const PdfMediaAnnotation& linked = result.annotations.at(0);
    QCOMPARE(linked.page_index, 0);
    QCOMPARE(linked.object_number, 5);
    QCOMPARE(linked.subtype, QStringLiteral("Movie"));
    QCOMPARE(linked.fileName, QStringLiteral("movies/clip.mov"));
    QCOMPARE(linked.rect, QRectF(10, 20, 20, 30));
    QCOMPARE(linked.resolved_file_path,
             QFileInfo(QDir(package_root), QStringLiteral("movies/clip.mov")).absoluteFilePath());

    const PdfMediaAnnotation& orphan = result.annotations.at(1);
    QCOMPARE(orphan.page_index, -1);
    QCOMPARE(orphan.object_number, 6);
    QCOMPARE(orphan.subtype, QStringLiteral("Sound"));
    QCOMPARE(orphan.fileName, QStringLiteral("orphan.wav"));
    QVERIFY(orphan.resolved_file_path.isEmpty());

    QVERIFY(result.summary().contains(QStringLiteral("page 1 Movie")));
    QVERIFY(result.summary().contains(QStringLiteral("unknown page Sound")));
}

void PdfMediaDetectorTest::traversal_media_path_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, movie_path_pdf_fixture(QByteArrayLiteral("../outside.mp4"))));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 1);
    QVERIFY(result.annotations.first().resolved_file_path.isEmpty());
    QVERIFY(!result.annotations.first().has_first_frame());
}

void PdfMediaDetectorTest::undeclared_package_media_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, movie_path_pdf_fixture(QByteArrayLiteral("movies/hidden.mp4"))));

    const PdfMediaScanResult result = scan_pdf_media_annotations(
        pdf_path,
        directory.filePath(QStringLiteral("package")),
        {QStringLiteral("movies/allowed.mp4")});
    QCOMPARE(result.annotations.size(), 1);
    QVERIFY(result.annotations.first().resolved_file_path.isEmpty());
    QVERIFY(!result.annotations.first().has_first_frame());
}

void PdfMediaDetectorTest::missing_pdf_returns_empty_result() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const PdfMediaScanResult result =
        scan_pdf_media_annotations(directory.filePath(QStringLiteral("missing.pdf")));

    QVERIFY(!result.has_media());
    QVERIFY(result.annotations.isEmpty());
}

#if defined(UIL_HAVE_FFMPEG)
void PdfMediaDetectorTest::ffmpeg_runtime_loads_lazily() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QString error_message;
    const QImage image = extract_first_video_frame(
        directory.filePath(QStringLiteral("missing.mp4")), &error_message);

    QVERIFY(image.isNull());
    QVERIFY2(error_message.startsWith(QStringLiteral("Could not open video:")),
             qPrintable(error_message));
}
#endif

QTEST_GUILESS_MAIN(PdfMediaDetectorTest)

#include "pdf_media_detector_test.moc"
