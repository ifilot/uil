#include "util/performance_log.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class PerformanceLogTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies that events, spans, and ordinary Qt messages are persisted. */
    void writes_structured_session_log();
};

void PerformanceLogTest::writes_structured_session_log() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(performance_log::initialize(directory.path()));
    performance_log::set_process_start_offset_ms(100);
    QVERIFY(performance_log::process_elapsed_ms() >= 100);

    performance_log::record_event(
        QStringLiteral("test.event"),
        {{QStringLiteral("answer"), 42}});
    {
        performance_log::ScopedSpan span(
            QStringLiteral("test.span"),
            {{QStringLiteral("item_count"), 3}});
        span.checkpoint(QStringLiteral("checkpoint"));
        span.set_outcome(QStringLiteral("verified"));
    }
    qInfo() << "ordinary Qt message";
    performance_log::flush();

    QFile log_file(performance_log::log_file_path());
    QVERIFY(log_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = log_file.readAll();

    QVERIFY(contents.contains("session.started"));
    QVERIFY(contents.contains("test.event"));
    QVERIFY(contents.contains("\"answer\":42"));
    QVERIFY(contents.contains("test.span.checkpoint"));
    QVERIFY(contents.contains("\"outcome\":\"verified\""));
    QVERIFY(contents.contains("ordinary Qt message"));
}

QTEST_GUILESS_MAIN(PerformanceLogTest)

#include "performance_log_test.moc"
