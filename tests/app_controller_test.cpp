#include "app_controller.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "pdf/qt_pdf_backend.hpp"
#include "ui/atomic_orbital_widget.hpp"
#include "ui/audience_window.hpp"

namespace {
QString example_path(const QString& file_name) {
    return QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/bundled/") + file_name;
}

QImage test_overlay() {
    QImage image(640, 360, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.fillRect(QRect(80, 80, 240, 120), QColor(255, 0, 0, 180));
    return image;
}
}  // namespace

class AppControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void empty_controller_rejects_document_operations();
    void opens_renders_and_navigates_bundled_document();
    void opens_bundled_molecule_presentation();
    void opens_bundled_atomic_orbital_presentation();
    /** @brief Exercises rapid atlas jumps and measures responsive navigation with four orbitals. */
    void orbital_atlas_navigation();
    void failed_open_preserves_current_document();
    void saves_and_reopens_annotation_package();
    void exports_annotated_pdf();
};

void AppControllerTest::empty_controller_rejects_document_operations() {
    AppController controller;
    QSignalSpy page_spy(&controller, &AppController::page_changed);
    QCOMPARE(controller.page_count(), 0);
    QCOMPARE(controller.current_page(), 0);

    controller.next_page();
    controller.previous_page();
    controller.go_to_page(8);
    QCOMPARE(page_spy.size(), 0);

    QString error_message;
    QVERIFY(!controller.save_uil_package(
        QStringLiteral("unused.uil"), {}, {}, true, &error_message));
    QCOMPARE(error_message, QStringLiteral("No presentation is open"));
    error_message.clear();
    QVERIFY(!controller.export_annotated_pdf(
        QStringLiteral("unused.pdf"), {}, &error_message));
    QCOMPARE(error_message, QStringLiteral("No presentation is open"));
}

void AppControllerTest::opens_renders_and_navigates_bundled_document() {
    AppController controller;
    QSignalSpy document_spy(&controller, &AppController::document_changed);
    QSignalSpy page_spy(&controller, &AppController::page_changed);
    QImage rendered_image;
    connect(
        &controller,
        &AppController::current_slide_image_changed,
        this,
        [&rendered_image](const QImage& image) {
            if (!image.isNull()) {
                rendered_image = image;
            }
        });

    const QString path = example_path(QStringLiteral("getting-started.pdf"));
    QVERIFY(controller.open_pdf(path));
    QCOMPARE(controller.current_path(), path);
    QCOMPARE(controller.page_count(), 4);
    QCOMPARE(controller.current_page(), 0);
    QCOMPARE(document_spy.size(), 1);
    QCOMPARE(document_spy.constFirst().constFirst().toInt(), 4);
    QTRY_VERIFY_WITH_TIMEOUT(!rendered_image.isNull(), 5000);

    page_spy.clear();
    controller.previous_page();
    QCOMPARE(controller.current_page(), 0);
    QCOMPARE(page_spy.size(), 0);
    controller.go_to_page(99);
    QCOMPARE(controller.current_page(), 3);
    QCOMPARE(page_spy.size(), 1);
    controller.next_page();
    QCOMPARE(controller.current_page(), 3);
    QCOMPARE(page_spy.size(), 1);
    controller.go_to_page(-50);
    QCOMPARE(controller.current_page(), 0);
    QCOMPARE(page_spy.size(), 2);
}

void AppControllerTest::opens_bundled_molecule_presentation() {
    AppController controller;
    PdfMediaScanResult scan_result;
    connect(&controller, &AppController::media_scan_changed, this,
            [&scan_result](const PdfMediaScanResult& result) {
                scan_result = result;
            });

    const QString path = example_path(QStringLiteral("molecule-visualizer.pdf"));
    QVERIFY(controller.open_pdf(path));
    QCOMPARE(controller.page_count(), 5);
    QVERIFY(controller.current_package_path().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(scan_result.molecule_annotations.size(), 5, 5000);

    for (int index = 0; index < scan_result.molecule_annotations.size(); ++index) {
        const PdfMoleculeAnnotation& molecule = scan_result.molecule_annotations.at(index);
        QCOMPARE(molecule.page_index, index);
        QVERIFY2(molecule.is_ready(), qPrintable(molecule.error_message));
    }
    QVERIFY(!scan_result.molecule_annotations.at(0).geometry.has_vibration());
    QVERIFY(!scan_result.molecule_annotations.at(1).geometry.has_vibration());
    QVERIFY(scan_result.molecule_annotations.at(2).geometry.has_vibration());
    QVERIFY(scan_result.molecule_annotations.at(3).geometry.has_vibration());
    QVERIFY(!scan_result.molecule_annotations.at(4).geometry.has_vibration());

    const MoleculeGeometry& water = scan_result.molecule_annotations.at(2).geometry;
    for (const MoleculeAtom& atom : water.atoms) {
        QCOMPARE(atom.position.x(), 0.0f);
        QCOMPARE(atom.vibration.x(), 0.0f);
    }

    const MoleculeGeometry& benzene = scan_result.molecule_annotations.at(4).geometry;
    QCOMPARE(benzene.atoms.size(), 12);
    QCOMPARE(benzene.bonds.size(), 12);
    for (const MoleculeAtom& atom : benzene.atoms) {
        QCOMPARE(atom.position.x(), 0.0f);
    }
}

void AppControllerTest::opens_bundled_atomic_orbital_presentation() {
    AppController controller;
    PdfMediaScanResult scan_result;
    connect(&controller, &AppController::media_scan_changed, this,
            [&scan_result](const PdfMediaScanResult& result) {
                scan_result = result;
            });

    const QString path = example_path(QStringLiteral("atomic-orbitals.pdf"));
    QVERIFY(controller.open_pdf(path));
    QCOMPARE(controller.page_count(), 5);
    QVERIFY(controller.current_package_path().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(scan_result.atomic_orbital_annotations.size(), 6, 5000);

    const QStringList expected_names{QStringLiteral("1s"),   QStringLiteral("2s"),
                                     QStringLiteral("2s"),   QStringLiteral("2pz"),
                                     QStringLiteral("3dz2"), QStringLiteral("4fz(5z2-3r2)")};
    for (int index = 0; index < expected_names.size(); ++index) {
        const PdfAtomicOrbitalAnnotation& orbital =
            scan_result.atomic_orbital_annotations.at(index);
        QCOMPARE(orbital.page_index, std::max(0, index - 1));
        QVERIFY2(orbital.is_ready(), qPrintable(orbital.error_message));
        QCOMPARE(orbital.definition.orbital, expected_names.at(index));
    }
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    AudienceWindow window;
    window.resize(1280, 720);
    controller.set_audience_window(&window);
    window.show();
    QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<AtomicOrbitalWidget*>().size(), 2, 5000);
    const auto orbitals = window.findChildren<AtomicOrbitalWidget*>();
    QTRY_VERIFY_WITH_TIMEOUT(orbitals[0]->isVisible() && orbitals[1]->isVisible(), 5000);
    QCOMPARE(orbitals[0]->definition().orbital, QStringLiteral("1s"));
    QCOMPARE(orbitals[1]->definition().orbital, QStringLiteral("2s"));
    controller.next_page();
    QTRY_VERIFY_WITH_TIMEOUT(orbitals[0]->isVisible() && orbitals[1]->isHidden(), 5000);
    QCOMPARE(orbitals[0]->definition().orbital, QStringLiteral("2s"));
    controller.previous_page();
    QTRY_VERIFY_WITH_TIMEOUT(orbitals[0]->isVisible() && orbitals[1]->isVisible(), 5000);
}

void AppControllerTest::orbital_atlas_navigation() {
  if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
    QSKIP("Requires QOpenGLWidget");
  AppController controller;
  AudienceWindow window;
  PdfMediaScanResult scan;
  connect(&controller, &AppController::media_scan_changed, this,
          [&](const PdfMediaScanResult& result) { scan = result; });
  controller.set_audience_window(&window);
  window.show();
  QVERIFY(controller.open_pdf(example_path("orbital-atlas.pdf")));
  QTRY_COMPARE_WITH_TIMEOUT(scan.atomic_orbital_annotations.size(), 55, 10000);
  const auto matches_page = [&](int page) {
    QVector<AtomicOrbitalDefinition> expected;
    for (const auto& annotation : scan.atomic_orbital_annotations)
      if (annotation.page_index == page) expected.push_back(annotation.definition);
    const auto widgets = window.findChildren<AtomicOrbitalWidget*>();
    if (widgets.size() < expected.size()) return false;
    for (int i = 0; i < widgets.size(); ++i) {
      if (i < expected.size()) {
        if (!widgets[i]->isVisible() || widgets[i]->definition() != expected[i]) return false;
      } else if (widgets[i]->isVisible())
        return false;
    }
    return true;
  };
  QTRY_VERIFY_WITH_TIMEOUT(matches_page(0), 15000);
  QElapsedTimer timer;
  timer.start();
  controller.go_to_page(13);
  controller.go_to_page(7);
  controller.go_to_page(12);
  const double jump_ms = timer.nsecsElapsed() / 1e6;
  QTRY_VERIFY_WITH_TIMEOUT(matches_page(12), 15000);
  controller.go_to_page(0);
  QTRY_VERIFY_WITH_TIMEOUT(matches_page(0), 15000);
  timer.restart();
  controller.go_to_page(12);
  const double warm_ms = timer.nsecsElapsed() / 1e6;
  QTRY_VERIFY_WITH_TIMEOUT(matches_page(12), 15000);
  qInfo(
      "Atlas: three cold navigation requests %.3f ms total; cached four-orbital transition %.3f ms",
      jump_ms, warm_ms);
  // Opening an unrelated document while work is in flight cannot revive old orbitals.
  controller.go_to_page(8);
  QVERIFY(controller.open_pdf(example_path("getting-started.pdf")));
  QTRY_VERIFY(scan.atomic_orbital_annotations.isEmpty());
  for (auto* widget : window.findChildren<AtomicOrbitalWidget*>()) QVERIFY(widget->isHidden());
}

void AppControllerTest::failed_open_preserves_current_document() {
    AppController controller;
    const QString valid_path =
        example_path(QStringLiteral("pointer-and-annotations.pdf"));
    QVERIFY(controller.open_pdf(valid_path));
    QCOMPARE(controller.page_count(), 3);
    QSignalSpy status_spy(&controller, &AppController::status_message_changed);

    QVERIFY(!controller.open_pdf(
        example_path(QStringLiteral("missing.pdf"))));
    QCOMPARE(controller.current_path(), valid_path);
    QCOMPARE(controller.page_count(), 3);
    QVERIFY(!status_spy.isEmpty());
    QVERIFY(status_spy.constLast().constFirst().toString().contains(
        QStringLiteral("Could not open PDF")));
}

void AppControllerTest::saves_and_reopens_annotation_package() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString package_path = directory.filePath(QStringLiteral("annotated.uil"));
    AppController source;
    QVERIFY(source.open_pdf(example_path(QStringLiteral("getting-started.pdf"))));
    const QImage overlay = test_overlay();
    QString error_message;
    QVERIFY2(source.save_uil_package(
        package_path,
        {{1, overlay}},
        QSet<int>{1},
        false,
        &error_message), qPrintable(error_message));
    QVERIFY(QFileInfo::exists(package_path));

    AppController restored;
    QVERIFY(restored.open_pdf(package_path));
    QCOMPARE(restored.page_count(), 4);
    QCOMPARE(restored.current_package_path(), QFileInfo(package_path).absoluteFilePath());
    QVERIFY(restored.loaded_overlay_images().contains(1));
    const QImage restored_overlay = restored.loaded_overlay_images().value(1);
    QCOMPARE(restored_overlay.size(), overlay.size());
    QCOMPARE(restored_overlay.convertToFormat(overlay.format()), overlay);
    QCOMPARE(restored.loaded_hidden_overlay_pages(), QSet<int>{1});
    QVERIFY(!restored.loaded_overlays_globally_visible());

    restored.clear_annotation_overlay_for_page(1);
    QVERIFY(restored.loaded_overlay_images().isEmpty());
    restored.clear_all_annotation_overlays();
    QVERIFY(restored.loaded_overlay_images().isEmpty());
}

void AppControllerTest::exports_annotated_pdf() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString output_path = directory.filePath(QStringLiteral("export.pdf"));
    AppController controller;
    QVERIFY(controller.open_pdf(
        example_path(QStringLiteral("pointer-and-annotations.pdf"))));
    QString error_message;
    QVERIFY2(controller.export_annotated_pdf(
        output_path,
        {{0, test_overlay()}},
        &error_message), qPrintable(error_message));

    QtPdfBackend exported;
    QVERIFY2(exported.open(output_path, &error_message), qPrintable(error_message));
    QCOMPARE(exported.page_count(), 3);
    QVERIFY(!exported.render_page(0, QSize(640, 360)).isNull());
}

QTEST_MAIN(AppControllerTest)

#include "app_controller_test.moc"
