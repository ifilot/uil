#include "util/document_hash.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
/** @brief Replaces a test file with the supplied bytes. */
bool write_test_file(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}
}

class DocumentHashTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies that repeated fingerprints of unchanged metadata are stable. */
    void unchanged_file_has_stable_hash();

    /** @brief Verifies that relative and absolute spellings identify the same file. */
    void equivalent_paths_have_equal_hashes();

    /** @brief Verifies that a file-size change invalidates its fingerprint. */
    void size_change_updates_hash();

    /** @brief Verifies that missing paths still receive deterministic distinct fingerprints. */
    void missing_paths_are_deterministic();
};

void DocumentHashTest::unchanged_file_has_stable_hash() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_test_file(path, QByteArrayLiteral("first revision")));

    const QString first = document_hash_for_file(path);
    QCOMPARE(first.size(), 64);
    QCOMPARE(document_hash_for_file(path), first);
}

void DocumentHashTest::equivalent_paths_have_equal_hashes() {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("hash-test-XXXXXX")));
    QVERIFY(directory.isValid());
    const QString absolute_path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_test_file(absolute_path, QByteArrayLiteral("presentation")));

    const QString relative_path = QDir::current().relativeFilePath(absolute_path);
    QCOMPARE(document_hash_for_file(relative_path), document_hash_for_file(absolute_path));
}

void DocumentHashTest::size_change_updates_hash() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("deck.pdf"));
    QVERIFY(write_test_file(path, QByteArrayLiteral("short")));
    const QString original = document_hash_for_file(path);

    QVERIFY(write_test_file(path, QByteArrayLiteral("a longer presentation")));
    QVERIFY(document_hash_for_file(path) != original);
}

void DocumentHashTest::missing_paths_are_deterministic() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first_path = directory.filePath(QStringLiteral("missing-a.pdf"));
    const QString second_path = directory.filePath(QStringLiteral("missing-b.pdf"));

    QCOMPARE(document_hash_for_file(first_path), document_hash_for_file(first_path));
    QVERIFY(document_hash_for_file(first_path) != document_hash_for_file(second_path));
}

QTEST_GUILESS_MAIN(DocumentHashTest)

#include "document_hash_test.moc"
