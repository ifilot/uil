#include "app_controller.hpp"
#include "pdf/qt_pdf_backend.hpp"

#include <QFileInfo>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
