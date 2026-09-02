#include "util/example_presentations.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace {
bool write_fixture(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(QByteArrayLiteral("fixture")) == 7;
}
}  // namespace

class ExamplePresentationsTest final : public QObject {
    Q_OBJECT

private slots:
    void missing_directory_has_no_examples();
    void discovers_known_examples_in_menu_order();
    void ignores_unknown_files_and_directories();
};

void ExamplePresentationsTest::missing_directory_has_no_examples() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QVERIFY(installed_example_presentations(directory.path()).isEmpty());
}

void ExamplePresentationsTest::discovers_known_examples_in_menu_order() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir application_directory(directory.path());
    QVERIFY(application_directory.mkpath(QStringLiteral("examples")));
    const QDir examples_directory(
        application_directory.filePath(QStringLiteral("examples")));

    QVERIFY(write_fixture(
        examples_directory.filePath(QStringLiteral("pointer-and-annotations.pdf"))));
    QVERIFY(write_fixture(
        examples_directory.filePath(QStringLiteral("getting-started.pdf"))));

    const QVector<ExamplePresentation> examples =
        installed_example_presentations(application_directory.absolutePath());
    QCOMPARE(examples.size(), 2);
    QCOMPARE(examples.at(0).title, QStringLiteral("Getting Started with uil"));
    QCOMPARE(examples.at(0).file_name, QStringLiteral("getting-started.pdf"));
    QCOMPARE(
        examples.at(0).absolute_path,
        QFileInfo(examples_directory.filePath(QStringLiteral("getting-started.pdf")))
            .absoluteFilePath());
    QCOMPARE(examples.at(1).title, QStringLiteral("Pointer and Annotation Tools"));
    QCOMPARE(
        examples.at(1).file_name,
        QStringLiteral("pointer-and-annotations.pdf"));
}

void ExamplePresentationsTest::ignores_unknown_files_and_directories() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir application_directory(directory.path());
    QVERIFY(application_directory.mkpath(QStringLiteral("examples/getting-started.pdf")));
    const QDir examples_directory(
        application_directory.filePath(QStringLiteral("examples")));
    QVERIFY(write_fixture(examples_directory.filePath(QStringLiteral("other.pdf"))));
    QVERIFY(write_fixture(
        examples_directory.filePath(QStringLiteral("pointer-and-annotations.pdf"))));

    const QVector<ExamplePresentation> examples =
        installed_example_presentations(application_directory.absolutePath());
    QCOMPARE(examples.size(), 1);
    QCOMPARE(
        examples.constFirst().file_name,
        QStringLiteral("pointer-and-annotations.pdf"));
}

QTEST_GUILESS_MAIN(ExamplePresentationsTest)

#include "example_presentations_test.moc"
