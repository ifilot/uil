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

/** @brief Returns a one-page PDF fixture containing a dedicated molecule annotation. */
QByteArray molecule_pdf_fixture() {
    return QByteArrayLiteral(
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>\nendobj\n"
        "4 0 obj\n<< /Type /Annot /Subtype /UILMolecule /Rect [10 20 110 220] "
        "/UIL << /Version 1 /F (molecules/water.xyz) >> >>\nendobj\n"
        "%%EOF\n");
}

/** @brief Returns a PDF containing a complete interactive figure in an embedded-file stream. */
QByteArray interactive_figure_pdf_fixture() {
    const QByteArray payload = QByteArrayLiteral(
        "{\"format\":\"uil.interactive-figure\",\"version\":1,"
        "\"title\":\"Embedded wave\","
        "\"background_svg\":\"<svg xmlns='http://www.w3.org/2000/svg' "
        "viewBox='0 0 800 500'><rect width='800' height='500' fill='#f8fafc'/></svg>\","
        "\"plot\":{\"kind\":\"sine-wave\",\"color\":\"#2563eb\","
        "\"x_min\":-6.28,\"x_max\":6.28,\"y_min\":-2.5,\"y_max\":2.5,"
        "\"x_label\":\"time\",\"y_label\":\"signal\"},"
        "\"controls\":{\"amplitude\":{\"min\":0,\"max\":2,\"value\":1},"
        "\"frequency\":{\"min\":0.25,\"max\":3,\"value\":1},\"animate\":true}}\n");
    return QByteArrayLiteral(
        "%PDF-1.7\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>\nendobj\n"
        "4 0 obj\n<< /Type /Annot /Subtype /UILInteractiveFigure "
        "/Rect [10 20 410 320] /UIL << /Version 1 /Asset 5 0 R >> >>\nendobj\n"
        "5 0 obj\n<< /Type /Filespec /F (wave.uilfig) /UF (wave.uilfig) "
        "/EF << /F 6 0 R >> >>\nendobj\n"
        "6 0 obj\n<< /Type /EmbeddedFile /Subtype /application#2Fvnd.uil.figure "
        "/Length ")
        + QByteArray::number(payload.size())
        + QByteArrayLiteral(" >>\nstream\n")
        + payload
        + QByteArrayLiteral("endstream\nendobj\n%%EOF\n");
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

    /** @brief Verifies dedicated molecule annotation detection and XYZ loading. */
    void molecule_annotation_loads_xyz_geometry();

    /** @brief Verifies extraction and validation of a self-contained embedded figure. */
    void embedded_interactive_figure_loads();

    /** @brief Verifies the bundled LaTeX-produced example end to end. */
    void bundled_interactive_figure_example_loads();

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

void PdfMediaDetectorTest::molecule_annotation_loads_xyz_geometry() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("molecules"))));
    const QString pdf_path = directory.filePath(QStringLiteral("deck.pdf"));
    const QString xyz_path = directory.filePath(QStringLiteral("molecules/water.xyz"));
    QVERIFY(write_pdf_fixture(pdf_path, molecule_pdf_fixture()));
    QVERIFY(write_pdf_fixture(
        xyz_path,
        QByteArrayLiteral(
            "3\nWater\n"
            "O 0.0000 0.0000 0.0000\n"
            "H 0.9572 0.0000 0.0000\n"
            "H -0.2390 0.9270 0.0000\n")));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 0);
    QCOMPARE(result.molecule_annotations.size(), 1);
    const PdfMoleculeAnnotation& molecule = result.molecule_annotations.first();
    QCOMPARE(molecule.page_index, 0);
    QCOMPARE(molecule.object_number, 4);
    QCOMPARE(molecule.file_name, QStringLiteral("molecules/water.xyz"));
    QCOMPARE(molecule.rect, QRectF(10, 20, 100, 200));
    QVERIFY2(molecule.is_ready(), qPrintable(molecule.error_message));
    QCOMPARE(molecule.geometry.atoms.size(), 3);
    QCOMPARE(molecule.geometry.bonds.size(), 2);
    QCOMPARE(molecule.geometry.description, QStringLiteral("Water"));
    QVERIFY(result.summary().contains(QStringLiteral("page 1 molecule")));
}

void PdfMediaDetectorTest::embedded_interactive_figure_loads() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("embedded-figure.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, interactive_figure_pdf_fixture()));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 0);
    QCOMPARE(result.molecule_annotations.size(), 0);
    QCOMPARE(result.interactive_figure_annotations.size(), 1);
    const PdfInteractiveFigureAnnotation& figure =
        result.interactive_figure_annotations.constFirst();
    QCOMPARE(figure.page_index, 0);
    QCOMPARE(figure.object_number, 4);
    QCOMPARE(figure.file_name, QStringLiteral("wave.uilfig"));
    QCOMPARE(figure.rect, QRectF(10, 20, 400, 300));
    QVERIFY2(figure.is_ready(), qPrintable(figure.error_message));
    QCOMPARE(figure.definition.title, QStringLiteral("Embedded wave"));
    QCOMPARE(figure.definition.x_label, QStringLiteral("time"));
    QCOMPARE(figure.definition.y_label, QStringLiteral("signal"));
    QCOMPARE(figure.definition.amplitude_initial, 1.0);
    QVERIFY(result.summary().contains(QStringLiteral("embedded figure ready")));
}

void PdfMediaDetectorTest::bundled_interactive_figure_example_loads() {
    const QString pdf_path = QStringLiteral(
        UIL_TEST_SOURCE_DIR "/examples/bundled/interactive-figure.pdf");
    QVERIFY2(QFileInfo::exists(pdf_path), qPrintable(pdf_path));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 0);
    QCOMPARE(result.molecule_annotations.size(), 0);
    QCOMPARE(result.interactive_figure_annotations.size(), 5);

    const PdfInteractiveFigureAnnotation& sine =
        result.interactive_figure_annotations.at(0);
    QCOMPARE(sine.page_index, 0);
    QCOMPARE(sine.file_name, QStringLiteral("moving-wave.uilfig"));
    QVERIFY2(sine.is_ready(), qPrintable(sine.error_message));
    QCOMPARE(sine.definition.title, QStringLiteral("A moving sine wave"));
    QCOMPARE(sine.definition.x_label, QStringLiteral("$x\\;\\mathrm{(radians)}$"));
    QCOMPARE(sine.definition.y_label, QStringLiteral("$y$"));

    const PdfInteractiveFigureAnnotation& figure =
        result.interactive_figure_annotations.at(1);
    QCOMPARE(figure.page_index, 1);
    QCOMPARE(figure.file_name, QStringLiteral("harmonic-wavepacket.uilfig"));
    QVERIFY2(figure.is_ready(), qPrintable(figure.error_message));
    QCOMPARE(
        figure.definition.kind,
        InteractiveFigureDefinition::Kind::HarmonicBondWavepacket);
    QCOMPARE(figure.definition.stretch_initial, 3.0);
    QCOMPARE(figure.definition.period_seconds, 8.0);
    QVERIFY(figure.definition.loop);
    QCOMPARE(figure.definition.x_label, QStringLiteral("$x = q / \\ell$"));

    const PdfInteractiveFigureAnnotation& basis =
        result.interactive_figure_annotations.at(2);
    QCOMPARE(basis.page_index, 2);
    QCOMPARE(basis.file_name, QStringLiteral("harmonic-basis-states.uilfig"));
    QVERIFY2(basis.is_ready(), qPrintable(basis.error_message));
    QCOMPARE(
        basis.definition.kind,
        InteractiveFigureDefinition::Kind::HarmonicBasisStates);
    QCOMPARE(basis.definition.basis_colors.size(), 6);
    QVERIFY(basis.definition.loop);
    QCOMPARE(
        basis.definition.title,
        QStringLiteral("A coherent packet and its real basis components"));

    const PdfInteractiveFigureAnnotation& step_fit =
        result.interactive_figure_annotations.at(3);
    QCOMPARE(step_fit.page_index, 3);
    QCOMPARE(
        step_fit.file_name,
        QStringLiteral("particle-in-box-step-expansion.uilfig"));
    QVERIFY2(step_fit.is_ready(), qPrintable(step_fit.error_message));
    QCOMPARE(
        step_fit.definition.kind,
        InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion);
    QCOMPARE(step_fit.definition.basis_count_min, 1);
    QCOMPARE(step_fit.definition.basis_count_max, 25);

    const PdfInteractiveFigureAnnotation& harmonic_fit =
        result.interactive_figure_annotations.at(4);
    QCOMPARE(harmonic_fit.page_index, 4);
    QCOMPARE(
        harmonic_fit.file_name,
        QStringLiteral("harmonic-displaced-state-expansion.uilfig"));
    QVERIFY2(harmonic_fit.is_ready(), qPrintable(harmonic_fit.error_message));
    QCOMPARE(
        harmonic_fit.definition.kind,
        InteractiveFigureDefinition::Kind::HarmonicDisplacedStateExpansion);
    QCOMPARE(harmonic_fit.definition.displacement, 2.0);
    QCOMPARE(harmonic_fit.definition.basis_count_min, 1);
    QCOMPARE(harmonic_fit.definition.basis_count_max, 25);
}

void PdfMediaDetectorTest::missing_pdf_returns_empty_result() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const PdfMediaScanResult result =
        scan_pdf_media_annotations(directory.filePath(QStringLiteral("missing.pdf")));

    QVERIFY(!result.has_media());
    QVERIFY(result.annotations.isEmpty());
    QVERIFY(result.molecule_annotations.isEmpty());
    QVERIFY(result.interactive_figure_annotations.isEmpty());
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
