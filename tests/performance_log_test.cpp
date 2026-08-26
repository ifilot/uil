#include "util/launcher_readiness.hpp"
#include "util/performance_log.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class PerformanceLogTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies that events, spans, and ordinary Qt messages are persisted. */
    void writes_structured_session_log();

#ifdef Q_OS_WIN
    /** @brief Verifies the named-event handshake used by the native launcher. */
    void signals_native_launcher_readiness_event();
#endif
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

#ifdef Q_OS_WIN
void PerformanceLogTest::signals_native_launcher_readiness_event() {
    const QString event_name = QStringLiteral("Local\\uil-readiness-test-%1")
                                   .arg(QCoreApplication::applicationPid());
    HANDLE event_handle = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        reinterpret_cast<LPCWSTR>(event_name.utf16()));
    QVERIFY(event_handle != nullptr);

    QByteArray ready_argument =
        (QStringLiteral("--uil-ready-event=") + event_name).toLocal8Bit();
    char executable_name[] = "uil-viewer.exe";
    char* arguments[] = {executable_name, ready_argument.data()};
    launcher_readiness::initialize(2, arguments);

    QVERIFY(launcher_readiness::is_active());
    launcher_readiness::signal_ready();
    QCOMPARE(WaitForSingleObject(event_handle, 0), DWORD(WAIT_OBJECT_0));
    CloseHandle(event_handle);
}
#endif

QTEST_GUILESS_MAIN(PerformanceLogTest)

#include "performance_log_test.moc"
