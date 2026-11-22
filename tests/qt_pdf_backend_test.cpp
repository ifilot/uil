#include "pdf/qt_pdf_backend.hpp"

#include <QFile>
#include <QImage>
#include <QSize>
#include <QTemporaryDir>
#include <QTest>

namespace {
QString example_path(const QString& file_name) {
    return QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/bundled/") + file_name;
}
}  // namespace

class QtPdfBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void opens_bundled_presentations_data();
    void opens_bundled_presentations();
    void renders_requested_dimensions();
    void rejects_invalid_page_and_render_requests();
    void reports_missing_and_malformed_documents();
    void reports_password_protected_document();
    void supports_mixed_page_sizes();
    /** @brief Verifies in-memory loading is independent of the file and yields premultiplied pages.
     */
    void opens_in_memory_copy_independent_of_file();
};

void QtPdfBackendTest::opens_bundled_presentations_data() {
    QTest::addColumn<QString>("file_name");
    QTest::addColumn<int>("expected_pages");

    QTest::newRow("getting started") << QStringLiteral("getting-started.pdf") << 4;
    QTest::newRow("pointer and annotations") << QStringLiteral("pointer-and-annotations.pdf") << 3;
}

void QtPdfBackendTest::opens_bundled_presentations() {
    QFETCH(QString, file_name);
    QFETCH(int, expected_pages);

    QtPdfBackend backend;
    QString error_message;
    QVERIFY2(backend.open(example_path(file_name), &error_message), qPrintable(error_message));
    QCOMPARE(backend.page_count(), expected_pages);

    const QSizeF page_size = backend.page_size_points(0);
    QVERIFY(page_size.width() > page_size.height());
    QVERIFY(qAbs(page_size.width() / page_size.height() - (16.0 / 9.0)) < 0.02);
}

void QtPdfBackendTest::renders_requested_dimensions() {
    QtPdfBackend backend;
    QVERIFY(backend.open(example_path(QStringLiteral("getting-started.pdf"))));

    const QImage image = backend.render_page(1, QSize(640, 360));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(640, 360));
}

void QtPdfBackendTest::rejects_invalid_page_and_render_requests() {
    QtPdfBackend backend;
    QVERIFY(backend.open(example_path(QStringLiteral("pointer-and-annotations.pdf"))));

    QVERIFY(!backend.page_size_points(-1).isValid());
    QVERIFY(!backend.page_size_points(backend.page_count()).isValid());
    QVERIFY(backend.render_page(-1, QSize(320, 180)).isNull());
    QVERIFY(backend.render_page(backend.page_count(), QSize(320, 180)).isNull());
    QVERIFY(backend.render_page(0, QSize()).isNull());
    QVERIFY(backend.render_page(0, QSize(-1, 180)).isNull());
}

void QtPdfBackendTest::reports_missing_and_malformed_documents() {
    QtPdfBackend missing_backend;
    QString missing_error;
    QVERIFY(
        !missing_backend.open(example_path(QStringLiteral("does-not-exist.pdf")), &missing_error));
    QVERIFY(!missing_error.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile malformed_file(directory.filePath(QStringLiteral("malformed.pdf")));
    QVERIFY(malformed_file.open(QIODevice::WriteOnly));
    QCOMPARE(malformed_file.write(QByteArrayLiteral("not a PDF")), qint64(9));
    malformed_file.close();

    QtPdfBackend malformed_backend;
    QString malformed_error;
    QVERIFY(!malformed_backend.open(malformed_file.fileName(), &malformed_error));
    QVERIFY(!malformed_error.isEmpty());
}

void QtPdfBackendTest::reports_password_protected_document() {
    QtPdfBackend backend;
    QString error_message;
    QVERIFY(
        !backend.open(QStringLiteral(UIL_TEST_SOURCE_DIR "/tests/fixtures/password-protected.pdf"),
                      &error_message));
    QCOMPARE(error_message, QStringLiteral("PDF requires a password"));
}

void QtPdfBackendTest::supports_mixed_page_sizes() {
    QtPdfBackend backend;
    QString error_message;
    QVERIFY2(
        backend.open(QStringLiteral(UIL_TEST_SOURCE_DIR "/tests/fixtures/mixed-page-sizes.pdf"),
                     &error_message),
        qPrintable(error_message));
    QCOMPARE(backend.page_count(), 2);
    const QSizeF landscape = backend.page_size_points(0);
    const QSizeF portrait = backend.page_size_points(1);
    QVERIFY(landscape.width() > landscape.height());
    QVERIFY(portrait.height() > portrait.width());
    QCOMPARE(backend.render_page(0, QSize(400, 225)).size(), QSize(400, 225));
    QCOMPARE(backend.render_page(1, QSize(300, 500)).size(), QSize(300, 500));
}

void QtPdfBackendTest::opens_in_memory_copy_independent_of_file() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(QFile::copy(example_path(QStringLiteral("getting-started.pdf")), path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    file.close();

    QtPdfBackend backend;
    QString error_message;
    QVERIFY2(backend.open_data(data, &error_message), qPrintable(error_message));
    QCOMPARE(backend.page_count(), 4);

    // Rewriting the source must not affect the open document.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not a pdf");
    file.close();
    const QImage image = backend.render_page(3, QSize(320, 180));
    QVERIFY(!image.isNull());
    QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);

    QtPdfBackend malformed;
    QVERIFY(!malformed.open_data(QByteArrayLiteral("not a pdf"), &error_message));
    QVERIFY(!error_message.isEmpty());
    QCOMPARE(malformed.page_count(), 0);
}

QTEST_GUILESS_MAIN(QtPdfBackendTest)

#include "qt_pdf_backend_test.moc"
