#include "util/pdf_media_detector.hpp"

#include "media/video_frame_extractor.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>

#include <zlib.h>

namespace {
/**
 * Upper bound for scanning each hostile fixture. Before the hardening these inputs crashed, ran
 * for minutes, or took about 30 s; the margin absorbs heavily loaded CI machines.
 */
constexpr qint64 kScanTimeLimitMs = 5000;

/** @brief Writes a synthetic PDF fixture to disk. */
bool write_pdf_fixture(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size();
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
               "4 0 obj\n<< /Type /Annot /Subtype /Movie /Rect [0 0 10 10] /F (") +
           movie_path + QByteArrayLiteral(") >>\nendobj\n%%EOF\n");
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
               "/Length ") +
           QByteArray::number(payload.size()) + QByteArrayLiteral(" >>\nstream\n") + payload +
           QByteArrayLiteral("endstream\nendobj\n%%EOF\n");
}

/** @brief Returns a PDF containing an embedded atomic-orbital definition. */
QByteArray atomic_orbital_pdf_fixture() {
    const QByteArray payload = QByteArrayLiteral(
        "{\"format\":\"uil.atomic-orbital\",\"version\":1,"
        "\"title\":\"Embedded 2p z\",\"orbital\":\"2pz\","
        "\"surface\":{\"positive_color\":\"#2563eb\","
        "\"negative_color\":\"#dc2626\",\"grid_size\":33},"
        "\"sampling_plane\":{\"type\":\"xz\","
        "\"offset\":{\"min\":-1,\"max\":1,\"value\":0.25}},"
        "\"contour\":{\"colormap\":\"RdBu_r\","
        "\"maximum\":0.01,\"levels\":14}}\n");
    return QByteArrayLiteral(
               "%PDF-1.7\n"
               "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
               "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
               "3 0 obj\n<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>\nendobj\n"
               "4 0 obj\n<< /Type /Annot /Subtype /UILAtomicOrbital "
               "/Rect [12 24 612 424] /UIL << /Version 1 /Asset 5 0 R >> >>\nendobj\n"
               "5 0 obj\n<< /Type /Filespec /F (2pz.uilorb) /UF (2pz.uilorb) "
               "/EF << /F 6 0 R >> >>\nendobj\n"
               "6 0 obj\n<< /Type /EmbeddedFile /Subtype /application#2Fvnd.uil.atomic-orbital "
               "/Length ") +
           QByteArray::number(payload.size()) + QByteArrayLiteral(" >>\nstream\n") + payload +
           QByteArrayLiteral("endstream\nendobj\n%%EOF\n");
}

/** @brief Returns one complete `N 0 obj ... endobj` definition. */
QByteArray pdf_object(int object_number, const QByteArray& body) {
    return QByteArray::number(object_number) + " 0 obj\n" + body + "\nendobj\n";
}

/** @brief Returns a Movie annotation body referencing @p file_name. */
QByteArray movie_annotation_body(const QByteArray& file_name) {
    return "<< /Type /Annot /Subtype /Movie /Rect [0 0 10 10] /F (" + file_name + ") >>";
}

/** @brief Returns a PDF whose page tree is a chain of @p depth nested /Pages nodes. */
QByteArray deeply_nested_page_tree_fixture(int depth) {
    QByteArray pdf = "%PDF-1.4\n" + pdf_object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.reserve(qsizetype(depth) * 72);
    for (int level = 0; level < depth; ++level) {
        pdf += pdf_object(2 + level, "<< /Type /Pages /Kids [" + QByteArray::number(3 + level) +
                                         " 0 R] /Count 1 >>");
    }
    const int leaf = 2 + depth;
    pdf += pdf_object(leaf, "<< /Type /Page /Annots [" + QByteArray::number(leaf + 1) + " 0 R] >>");
    pdf += pdf_object(leaf + 1, movie_annotation_body("deep.mov"));
    return pdf + "%%EOF\n";
}

/** @brief Returns a valid one-page movie PDF followed by @p count headers sharing one `endobj`. */
QByteArray unterminated_objects_fixture(int count) {
    QByteArray pdf = "%PDF-1.4\n" + pdf_object(1, "<< /Type /Catalog /Pages 2 0 R >>") +
                     pdf_object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>") +
                     pdf_object(3, "<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>") +
                     pdf_object(4, movie_annotation_body("clip.mov"));
    const QByteArray padding(64, 'x');
    pdf.reserve(pdf.size() + qsizetype(count) * 100);
    for (int index = 0; index < count; ++index) {
        pdf += QByteArray::number(100 + index) + " 0 obj\n<< /Filler (" + padding + ") >>\n";
    }
    return pdf + "endobj\n%%EOF\n";
}

/** @brief Returns @p size zero bytes compressed with zlib, without materializing them. */
QByteArray deflated_zeros(qsizetype size) {
    z_stream stream{};
    if (deflateInit(&stream, Z_BEST_COMPRESSION) != Z_OK) {
        return {};
    }
    const QByteArray zeros(1024 * 1024, '\0');
    QByteArray output;
    std::array<char, 65536> buffer{};
    qsizetype remaining = size;
    int result = Z_OK;
    do {
        const qsizetype chunk = std::min<qsizetype>(remaining, zeros.size());
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zeros.constData()));
        stream.avail_in = uInt(chunk);
        remaining -= chunk;
        const int flush = remaining == 0 ? Z_FINISH : Z_NO_FLUSH;
        do {
            stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
            stream.avail_out = uInt(buffer.size());
            result = deflate(&stream, flush);
            output.append(buffer.data(), qsizetype(buffer.size() - stream.avail_out));
        } while (stream.avail_out == 0);
    } while (remaining > 0);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray{};
}
}  // namespace

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

    /** @brief Verifies extraction and validation of an embedded atomic orbital. */
    void embedded_atomic_orbital_loads();

    /** @brief Verifies the bundled atomic-orbital presentation end to end. */
    void bundled_atomic_orbital_example_loads();
    /** @brief Checks every embedded orbital in the generated Garnet-Slate atlas. */
    void bundled_orbital_atlas_loads();

    /** @brief Verifies all five bundled molecular-symmetry players end to end. */
    void bundled_molecular_symmetry_example_loads();

    /** @brief Verifies that a 100k-level /Pages chain neither overflows the stack nor hangs. */
    void deeply_nested_page_tree_is_bounded();

    /** @brief Verifies that many `obj` headers sharing one `endobj` scan in linear time. */
    void unterminated_objects_scan_quickly();

    /** @brief Verifies that the object-header scanner stays linear on a long digit run. */
    void long_digit_run_scans_quickly();

    /** @brief Verifies that reference cycles in the page tree terminate with correct page order. */
    void page_tree_reference_cycle_terminates();

    /** @brief Verifies that decompression bombs in object streams share a bounded budget. */
    void object_stream_decompression_is_bounded();

    /** @brief Verifies that one large /Annots array shared by many pages is bounded. */
    void shared_annotation_array_is_bounded();

#if defined(UIL_HAVE_FFMPEG)
    /** @brief Verifies that the optional FFmpeg runtime can be loaded on first use. */
    void ffmpeg_runtime_loads_lazily();
#endif
};

void PdfMediaDetectorTest::annotation_helpers_report_state() {
    PdfMediaAnnotation annotation;
    QVERIFY(!annotation.has_first_frame());
    QVERIFY(!annotation.is_mp4());

    annotation.file_name = QStringLiteral("CLIP.MP4");
    QVERIFY(annotation.is_mp4());

    annotation.file_name.clear();
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

    const PdfMediaScanResult result =
        scan_pdf_media_annotations(pdf_path, package_root, {QStringLiteral("movies/clip.mov")});
    QVERIFY(result.has_media());
    QCOMPARE(result.annotations.size(), 2);

    const PdfMediaAnnotation& linked = result.annotations.at(0);
    QCOMPARE(linked.page_index, 0);
    QCOMPARE(linked.object_number, 5);
    QCOMPARE(linked.subtype, QStringLiteral("Movie"));
    QCOMPARE(linked.file_name, QStringLiteral("movies/clip.mov"));
    QCOMPARE(linked.rect, QRectF(10, 20, 20, 30));
    QCOMPARE(linked.resolved_file_path,
             QFileInfo(QDir(package_root), QStringLiteral("movies/clip.mov")).absoluteFilePath());

    const PdfMediaAnnotation& orphan = result.annotations.at(1);
    QCOMPARE(orphan.page_index, -1);
    QCOMPARE(orphan.object_number, 6);
    QCOMPARE(orphan.subtype, QStringLiteral("Sound"));
    QCOMPARE(orphan.file_name, QStringLiteral("orphan.wav"));
    QVERIFY(orphan.resolved_file_path.isEmpty());

    QVERIFY(result.summary().contains(QStringLiteral("page 1 Movie")));
    QVERIFY(result.summary().contains(QStringLiteral("unknown page Sound")));
}

void PdfMediaDetectorTest::traversal_media_path_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(
        write_pdf_fixture(pdf_path, movie_path_pdf_fixture(QByteArrayLiteral("../outside.mp4"))));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 1);
    QVERIFY(result.annotations.first().resolved_file_path.isEmpty());
    QVERIFY(!result.annotations.first().has_first_frame());
}

void PdfMediaDetectorTest::undeclared_package_media_is_rejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path,
                              movie_path_pdf_fixture(QByteArrayLiteral("movies/hidden.mp4"))));

    const PdfMediaScanResult result =
        scan_pdf_media_annotations(pdf_path, directory.filePath(QStringLiteral("package")),
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
    QVERIFY(write_pdf_fixture(xyz_path, QByteArrayLiteral("3\nWater\n"
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
    const QString pdf_path =
        QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/bundled/interactive-figure.pdf");
    QVERIFY2(QFileInfo::exists(pdf_path), qPrintable(pdf_path));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 0);
    QCOMPARE(result.molecule_annotations.size(), 0);
    QCOMPARE(result.interactive_figure_annotations.size(), 5);

    const PdfInteractiveFigureAnnotation& sine = result.interactive_figure_annotations.at(0);
    QCOMPARE(sine.page_index, 0);
    QCOMPARE(sine.file_name, QStringLiteral("moving-wave.uilfig"));
    QVERIFY2(sine.is_ready(), qPrintable(sine.error_message));
    QCOMPARE(sine.definition.title, QStringLiteral("A moving sine wave"));
    QCOMPARE(sine.definition.x_label, QStringLiteral("$x\\;\\mathrm{(radians)}$"));
    QCOMPARE(sine.definition.y_label, QStringLiteral("$y$"));

    const PdfInteractiveFigureAnnotation& figure = result.interactive_figure_annotations.at(1);
    QCOMPARE(figure.page_index, 1);
    QCOMPARE(figure.file_name, QStringLiteral("harmonic-wavepacket.uilfig"));
    QVERIFY2(figure.is_ready(), qPrintable(figure.error_message));
    QCOMPARE(figure.definition.kind, InteractiveFigureDefinition::Kind::HarmonicBondWavepacket);
    QCOMPARE(figure.definition.stretch_initial, 3.0);
    QCOMPARE(figure.definition.period_seconds, 8.0);
    QVERIFY(figure.definition.loop);
    QCOMPARE(figure.definition.x_label, QStringLiteral("$x = q / \\ell$"));

    const PdfInteractiveFigureAnnotation& basis = result.interactive_figure_annotations.at(2);
    QCOMPARE(basis.page_index, 2);
    QCOMPARE(basis.file_name, QStringLiteral("harmonic-basis-states.uilfig"));
    QVERIFY2(basis.is_ready(), qPrintable(basis.error_message));
    QCOMPARE(basis.definition.kind, InteractiveFigureDefinition::Kind::HarmonicBasisStates);
    QCOMPARE(basis.definition.basis_colors.size(), 6);
    QVERIFY(basis.definition.loop);
    QCOMPARE(basis.definition.title,
             QStringLiteral("A coherent packet and its real basis components"));

    const PdfInteractiveFigureAnnotation& step_fit = result.interactive_figure_annotations.at(3);
    QCOMPARE(step_fit.page_index, 3);
    QCOMPARE(step_fit.file_name, QStringLiteral("particle-in-box-step-expansion.uilfig"));
    QVERIFY2(step_fit.is_ready(), qPrintable(step_fit.error_message));
    QCOMPARE(step_fit.definition.kind,
             InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion);
    QCOMPARE(step_fit.definition.basis_count_min, 1);
    QCOMPARE(step_fit.definition.basis_count_max, 25);

    const PdfInteractiveFigureAnnotation& harmonic_fit =
        result.interactive_figure_annotations.at(4);
    QCOMPARE(harmonic_fit.page_index, 4);
    QCOMPARE(harmonic_fit.file_name, QStringLiteral("harmonic-displaced-state-expansion.uilfig"));
    QVERIFY2(harmonic_fit.is_ready(), qPrintable(harmonic_fit.error_message));
    QCOMPARE(harmonic_fit.definition.kind,
             InteractiveFigureDefinition::Kind::HarmonicDisplacedStateExpansion);
    QCOMPARE(harmonic_fit.definition.displacement, 2.0);
    QCOMPARE(harmonic_fit.definition.basis_count_min, 1);
    QCOMPARE(harmonic_fit.definition.basis_count_max, 25);
}

void PdfMediaDetectorTest::embedded_atomic_orbital_loads() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("embedded-orbital.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, atomic_orbital_pdf_fixture()));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 0);
    QCOMPARE(result.molecule_annotations.size(), 0);
    QCOMPARE(result.interactive_figure_annotations.size(), 0);
    QCOMPARE(result.atomic_orbital_annotations.size(), 1);
    const PdfAtomicOrbitalAnnotation& orbital = result.atomic_orbital_annotations.constFirst();
    QCOMPARE(orbital.page_index, 0);
    QCOMPARE(orbital.object_number, 4);
    QCOMPARE(orbital.file_name, QStringLiteral("2pz.uilorb"));
    QCOMPARE(orbital.rect, QRectF(12, 24, 600, 400));
    QVERIFY2(orbital.is_ready(), qPrintable(orbital.error_message));
    QCOMPARE(orbital.definition.title, QStringLiteral("Embedded 2p z"));
    QCOMPARE(orbital.definition.orbital, QStringLiteral("2pz"));
    QCOMPARE(orbital.definition.plane, AtomicOrbitalDefinition::Plane::XZ);
    QCOMPARE(orbital.definition.colormap, QStringLiteral("RdBu_r"));
    QCOMPARE(orbital.definition.contour_maximum, 0.01);
    QCOMPARE(orbital.definition.offset_initial, 0.25);
    QVERIFY(result.summary().contains(QStringLiteral("embedded orbital ready")));
}

void PdfMediaDetectorTest::bundled_atomic_orbital_example_loads() {
    const QString pdf_path =
        QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/bundled/atomic-orbitals.pdf");
    QVERIFY2(QFileInfo::exists(pdf_path), qPrintable(pdf_path));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.atomic_orbital_annotations.size(), 6);
    const QStringList expected_names{QStringLiteral("1s"),   QStringLiteral("2s"),
                                     QStringLiteral("2s"),   QStringLiteral("2pz"),
                                     QStringLiteral("3dz2"), QStringLiteral("4fz(5z2-3r2)")};
    for (int index = 0; index < expected_names.size(); ++index) {
        const PdfAtomicOrbitalAnnotation& orbital = result.atomic_orbital_annotations.at(index);
        QCOMPARE(orbital.page_index, std::max(0, index - 1));
        QVERIFY2(orbital.is_ready(), qPrintable(orbital.error_message));
        QCOMPARE(orbital.definition.orbital, expected_names.at(index));
    }
}

void PdfMediaDetectorTest::bundled_orbital_atlas_loads() {
    const auto result = scan_pdf_media_annotations(
        QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/bundled/orbital-atlas.pdf"));
    const auto catalog = atomic_orbital_catalog();
    QCOMPARE(result.atomic_orbital_annotations.size(), catalog.size());
    for (int index = 0; index < catalog.size(); ++index) {
        const auto& annotation = result.atomic_orbital_annotations.at(index);
        QVERIFY2(annotation.is_ready(), qPrintable(annotation.error_message));
        QCOMPARE(annotation.page_index, index / 4);
        QCOMPARE(annotation.definition.orbital, catalog.at(index).name);
        QCOMPARE(annotation.definition.colormap, QStringLiteral("garnet_slate"));
        for (int other = index / 4 * 4; other < index; ++other) {
            QVERIFY(!annotation.rect.intersects(result.atomic_orbital_annotations.at(other).rect));
        }
    }
}

void PdfMediaDetectorTest::bundled_molecular_symmetry_example_loads() {
    const QString pdf_path =
        QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/bundled/molecular-symmetry.pdf");
    QVERIFY2(QFileInfo::exists(pdf_path), qPrintable(pdf_path));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.molecular_symmetry_annotations.size(), 5);
    const QStringList expected_groups{QStringLiteral("C2v"), QStringLiteral("C3v"),
                                      QStringLiteral("D3h"), QStringLiteral("D2h"),
                                      QStringLiteral("Td")};
    const QList<int> expected_counts{4, 6, 12, 8, 24};
    for (int index = 0; index < expected_groups.size(); ++index) {
        const PdfMolecularSymmetryAnnotation& annotation =
            result.molecular_symmetry_annotations.at(index);
        QCOMPARE(annotation.page_index, index);
        QVERIFY2(annotation.is_ready(), qPrintable(annotation.error_message));
        QCOMPARE(annotation.definition.point_group, expected_groups.at(index));
        QCOMPARE(annotation.definition.operations.size(), expected_counts.at(index));
    }
    QVERIFY(result.summary().contains(QStringLiteral("embedded symmetry ready")));
}

void PdfMediaDetectorTest::deeply_nested_page_tree_is_bounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("deep.pdf"));
    constexpr int kDepth = 100000;
    QVERIFY(write_pdf_fixture(pdf_path, deeply_nested_page_tree_fixture(kDepth)));

    QElapsedTimer timer;
    timer.start();
    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QVERIFY2(timer.elapsed() < kScanTimeLimitMs, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(result.annotations.size(), 1);
    QCOMPARE(result.annotations.first().object_number, kDepth + 3);
    QCOMPARE(result.annotations.first().file_name, QStringLiteral("deep.mov"));
}

void PdfMediaDetectorTest::unterminated_objects_scan_quickly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("unterminated.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, unterminated_objects_fixture(100000)));

    QElapsedTimer timer;
    timer.start();
    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QVERIFY2(timer.elapsed() < kScanTimeLimitMs, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(result.annotations.size(), 1);
    QCOMPARE(result.annotations.first().page_index, 0);
    QCOMPARE(result.annotations.first().file_name, QStringLiteral("clip.mov"));
}

void PdfMediaDetectorTest::long_digit_run_scans_quickly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("digits.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, movie_path_pdf_fixture(QByteArrayLiteral("clip.mov")) +
                                            QByteArray(4 * 1024 * 1024, '7') +
                                            QByteArrayLiteral("\nendobj\n%%EOF\n")));

    QElapsedTimer timer;
    timer.start();
    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QVERIFY2(timer.elapsed() < kScanTimeLimitMs, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(result.annotations.size(), 1);
    QCOMPARE(result.annotations.first().page_index, 0);
}

void PdfMediaDetectorTest::page_tree_reference_cycle_terminates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf_path = directory.filePath(QStringLiteral("cycle.pdf"));
    QVERIFY(write_pdf_fixture(
        pdf_path, "%PDF-1.4\n" + pdf_object(1, "<< /Type /Catalog /Pages 2 0 R >>") +
                      pdf_object(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>") +
                      pdf_object(3, "<< /Type /Page /Parent 2 0 R /Annots [5 0 R] >>") +
                      pdf_object(4, "<< /Type /Pages /Kids [2 0 R 4 0 R 6 0 R 1 0 R] /Count 1 >>") +
                      pdf_object(5, movie_annotation_body("first.mov")) +
                      pdf_object(6, "<< /Type /Page /Parent 4 0 R /Annots [7 0 R] >>") +
                      pdf_object(7, movie_annotation_body("second.mov")) + "%%EOF\n"));

    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QCOMPARE(result.annotations.size(), 2);
    QCOMPARE(result.annotations.at(0).object_number, 5);
    QCOMPARE(result.annotations.at(0).page_index, 0);
    QCOMPARE(result.annotations.at(1).object_number, 7);
    QCOMPARE(result.annotations.at(1).page_index, 1);
}

void PdfMediaDetectorTest::object_stream_decompression_is_bounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bomb = deflated_zeros(65LL * 1024 * 1024);
    QVERIFY(!bomb.isEmpty());

    QByteArray pdf = movie_path_pdf_fixture(QByteArrayLiteral("clip.mov"));
    for (int index = 0; index < 128; ++index) {
        pdf +=
            pdf_object(100 + index, "<< /Type /ObjStm /N 1 /First 4 /Filter /FlateDecode /Length " +
                                        QByteArray::number(bomb.size()) + " >>\nstream\n" + bomb +
                                        "\nendstream");
    }
    const QString pdf_path = directory.filePath(QStringLiteral("bombs.pdf"));
    QVERIFY(write_pdf_fixture(pdf_path, pdf));

    QElapsedTimer timer;
    timer.start();
    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QVERIFY2(timer.elapsed() < kScanTimeLimitMs, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(result.annotations.size(), 1);
    QCOMPARE(result.annotations.first().page_index, 0);
}

void PdfMediaDetectorTest::shared_annotation_array_is_bounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int kPageCount = 20000;
    constexpr int kReferencesPerPage = 2000;
    constexpr int kFirstPage = 10;

    QByteArray kids;
    QByteArray pages;
    for (int page = 0; page < kPageCount; ++page) {
        kids += QByteArray::number(kFirstPage + page) + " 0 R ";
        pages += pdf_object(kFirstPage + page, "<< /Type /Page /Parent 2 0 R /Annots 3 0 R >>");
    }
    QByteArray annotation_array = "[";
    for (int reference = 0; reference < kReferencesPerPage; ++reference) {
        annotation_array += "4 0 R ";
    }
    annotation_array += "]";
    const QString pdf_path = directory.filePath(QStringLiteral("shared-annots.pdf"));
    QVERIFY(write_pdf_fixture(
        pdf_path, "%PDF-1.4\n" + pdf_object(1, "<< /Type /Catalog /Pages 2 0 R >>") +
                      pdf_object(2, "<< /Type /Pages /Kids [" + kids + "] >>") +
                      pdf_object(3, annotation_array) +
                      pdf_object(4, movie_annotation_body("clip.mov")) + pages + "%%EOF\n"));

    QElapsedTimer timer;
    timer.start();
    const PdfMediaScanResult result = scan_pdf_media_annotations(pdf_path);
    QVERIFY2(timer.elapsed() < kScanTimeLimitMs, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(!result.annotations.isEmpty());
    QVERIFY(result.annotations.size() <= 4096);
    QCOMPARE(result.annotations.first().page_index, 0);
    QCOMPARE(result.annotations.first().object_number, 4);
    QCOMPARE(result.annotations.last().file_name, QStringLiteral("clip.mov"));
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
    QVERIFY(result.molecular_symmetry_annotations.isEmpty());
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
