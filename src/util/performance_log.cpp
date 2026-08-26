#include "performance_log.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMessageLogger>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <cstdio>
#include <algorithm>
#include <utility>

namespace {

struct LogState {
    QMutex mutex;
    QFile file;
    QElapsedTimer timer;
    QtMessageHandler previous_handler = nullptr;
    QString file_path;
    QString session_id;
    qint64 last_flush_ms = 0;
    qint64 process_start_offset_ms = 0;
    bool initialized = false;
};

/** @brief Returns process-lifetime logger state. */
LogState& log_state() {
    static LogState* state = new LogState;
    return *state;
}

/** @brief Returns a stable text representation of a Qt message severity. */
QString severity_name(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARNING");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

/** @brief Replaces physical line breaks so every record occupies one log line. */
QString single_line_message(QString message) {
    message.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    message.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return message;
}

/** @brief Writes Qt messages to the active session log and the prior destination. */
void message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    LogState& state = log_state();
    QString line;
    QtMessageHandler previous_handler = nullptr;

    {
        QMutexLocker locker(&state.mutex);
        const double elapsed_ms = state.timer.isValid()
            ? double(state.timer.nsecsElapsed()) / 1'000'000.0
            : 0.0;
        const quintptr thread_id = reinterpret_cast<quintptr>(QThread::currentThreadId());
        const QString category = context.category && context.category[0] != '\0'
            ? QString::fromUtf8(context.category)
            : QStringLiteral("default");
        line = QStringLiteral("%1 +%2ms [%3] [%4] [thread=0x%5] %6\n")
                   .arg(
                       QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                       QString::number(elapsed_ms, 'f', 3),
                       severity_name(type),
                       category,
                       QString::number(thread_id, 16),
                       single_line_message(message));

        if (state.file.isOpen()) {
            state.file.write(line.toUtf8());
            const bool urgent = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
            if (urgent || elapsed_ms - double(state.last_flush_ms) >= 1000.0) {
                state.file.flush();
                state.last_flush_ms = qint64(elapsed_ms);
            }
        }
        previous_handler = state.previous_handler;
    }

    if (previous_handler) {
        previous_handler(type, context, message);
        return;
    }

    const QByteArray console_line = line.toLocal8Bit();
    std::fwrite(console_line.constData(), 1, size_t(console_line.size()), stderr);
    std::fflush(stderr);
}

/** @brief Emits a compact JSON payload through the performance log category. */
void record_payload(QJsonObject payload) {
    if (!performance_log::is_initialized()) {
        return;
    }

    const QByteArray json = QJsonDocument(std::move(payload)).toJson(QJsonDocument::Compact);
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "performance").info("%s", json.constData());
}

}  // namespace

namespace performance_log {

bool initialize(const QString& log_directory) {
    LogState& state = log_state();
    QString active_path;
    QString session_id;

    {
        QMutexLocker locker(&state.mutex);
        if (state.initialized) {
            return true;
        }

        const QString directory_path = log_directory.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                  .filePath(QStringLiteral("logs"))
            : log_directory;
        QDir directory(directory_path);
        if (!directory.mkpath(QStringLiteral("."))) {
            return false;
        }

        state.session_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString timestamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        const QString filename = QStringLiteral("uil-performance-%1-pid%2.log")
                                     .arg(timestamp)
                                     .arg(QCoreApplication::applicationPid());
        state.file_path = directory.filePath(filename);
        state.file.setFileName(state.file_path);
        if (!state.file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            state.file_path.clear();
            state.session_id.clear();
            return false;
        }

        state.timer.start();
        state.initialized = true;
        state.previous_handler = qInstallMessageHandler(message_handler);
        active_path = state.file_path;
        session_id = state.session_id;
    }

    record_event(QStringLiteral("session.started"), {
        {QStringLiteral("application"), QCoreApplication::applicationName()},
        {QStringLiteral("application_version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("log_file"), active_path},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("session_id"), session_id}
    });
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] {
            performance_log::record_event(QStringLiteral("session.ending"), {
                {QStringLiteral("session_elapsed_ms"), performance_log::session_elapsed_ms()}
            });
            performance_log::flush();
        });
    }
    return true;
}

bool is_initialized() {
    LogState& state = log_state();
    QMutexLocker locker(&state.mutex);
    return state.initialized;
}

QString log_file_path() {
    LogState& state = log_state();
    QMutexLocker locker(&state.mutex);
    return state.file_path;
}

qint64 session_elapsed_ms() {
    LogState& state = log_state();
    QMutexLocker locker(&state.mutex);
    return state.timer.isValid() ? state.timer.elapsed() : -1;
}

void set_process_start_offset_ms(qint64 elapsed_ms) {
    LogState& state = log_state();
    QMutexLocker locker(&state.mutex);
    state.process_start_offset_ms = std::max<qint64>(0, elapsed_ms);
}

qint64 process_elapsed_ms() {
    LogState& state = log_state();
    QMutexLocker locker(&state.mutex);
    const qint64 session_ms = state.timer.isValid() ? state.timer.elapsed() : 0;
    return state.process_start_offset_ms + session_ms;
}

void flush() {
    LogState& state = log_state();
    QMutexLocker locker(&state.mutex);
    if (state.file.isOpen()) {
        state.file.flush();
        state.last_flush_ms = state.timer.isValid() ? state.timer.elapsed() : 0;
    }
}

void record_event(const QString& name, const QVariantMap& fields) {
    QJsonObject payload{
        {QStringLiteral("kind"), QStringLiteral("event")},
        {QStringLiteral("name"), name},
        {QStringLiteral("fields"), QJsonObject::fromVariantMap(fields)}
    };
    record_payload(std::move(payload));
}

void record_duration(const QString& name, qint64 duration_ms, const QVariantMap& fields) {
    QJsonObject payload{
        {QStringLiteral("duration_ms"), duration_ms},
        {QStringLiteral("kind"), QStringLiteral("duration")},
        {QStringLiteral("name"), name},
        {QStringLiteral("fields"), QJsonObject::fromVariantMap(fields)}
    };
    record_payload(std::move(payload));
}

ScopedSpan::ScopedSpan(QString name, QVariantMap fields)
    : name_(std::move(name)),
      fields_(std::move(fields)) {
    timer_.start();
}

ScopedSpan::~ScopedSpan() {
    fields_.insert(QStringLiteral("outcome"), outcome_);
    record_duration(name_, timer_.elapsed(), fields_);
}

void ScopedSpan::add_field(const QString& key, const QVariant& value) {
    fields_.insert(key, value);
}

void ScopedSpan::set_outcome(const QString& outcome) {
    outcome_ = outcome;
}

void ScopedSpan::checkpoint(const QString& checkpoint_name, const QVariantMap& fields) {
    const qint64 total_ms = timer_.elapsed();
    QVariantMap checkpoint_fields = fields_;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        checkpoint_fields.insert(it.key(), it.value());
    }
    checkpoint_fields.insert(QStringLiteral("checkpoint"), checkpoint_name);
    checkpoint_fields.insert(QStringLiteral("total_ms"), total_ms);
    record_duration(
        name_ + QStringLiteral(".") + checkpoint_name,
        total_ms - last_checkpoint_ms_,
        checkpoint_fields);
    last_checkpoint_ms_ = total_ms;
}

qint64 ScopedSpan::elapsed_ms() const {
    return timer_.elapsed();
}

}  // namespace performance_log
