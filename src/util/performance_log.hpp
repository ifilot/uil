#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QVariantMap>

namespace performance_log {

/** @brief Initializes the per-session application log. */
bool initialize(const QString& log_directory = {});

/** @brief Returns whether the performance logger is ready to accept records. */
bool is_initialized();

/** @brief Returns the active session log file path. */
QString log_file_path();

/** @brief Returns milliseconds elapsed since logger initialization. */
qint64 session_elapsed_ms();

/** @brief Sets elapsed process time at the instant the logger was initialized. */
void set_process_start_offset_ms(qint64 elapsed_ms);

/** @brief Returns milliseconds elapsed since operating-system process creation. */
qint64 process_elapsed_ms();

/** @brief Flushes pending log records to the session file. */
void flush();

/** @brief Records a structured instantaneous event. */
void record_event(const QString& name, const QVariantMap& fields = {});

/** @brief Records a structured duration metric in milliseconds. */
void record_duration(const QString& name, qint64 duration_ms, const QVariantMap& fields = {});

/** @brief Measures a scope and optional checkpoints using structured log records. */
class ScopedSpan final {
public:
    /** @brief Starts measuring a named span. */
    explicit ScopedSpan(QString name, QVariantMap fields = {});

    /** @brief Records the completed span when it leaves scope. */
    ~ScopedSpan();

    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;
    ScopedSpan(ScopedSpan&&) = delete;
    ScopedSpan& operator=(ScopedSpan&&) = delete;

    /** @brief Adds or replaces a field included with the final span record. */
    void add_field(const QString& key, const QVariant& value);

    /** @brief Sets the outcome included with the final span record. */
    void set_outcome(const QString& outcome);

    /** @brief Records time since the previous checkpoint and total span time. */
    void checkpoint(const QString& checkpoint_name, const QVariantMap& fields = {});

    /** @brief Returns the milliseconds elapsed since the span started. */
    qint64 elapsed_ms() const;

private:
    QString name_;
    QVariantMap fields_;
    QElapsedTimer timer_;
    qint64 last_checkpoint_ms_ = 0;
    QString outcome_ = QStringLiteral("completed");
};

}  // namespace performance_log
