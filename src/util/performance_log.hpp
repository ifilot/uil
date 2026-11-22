#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QVariantMap>

/**
 * @brief Per-session application and performance log.
 *
 * All free functions in this namespace share one process-wide state guarded by a mutex, so
 * they may be called from any thread, including render workers. Before initialize() succeeds,
 * the record functions are no-ops.
 */
namespace performance_log {

/**
 * @brief Initializes the per-session application log.
 *
 * Creates @p log_directory if needed, opens a new `uil-performance-<timestamp>-pid<pid>.log`
 * file in it, starts the session timer, installs a process-wide Qt message handler and records
 * a `session.started` event. The handler writes every Qt message (not only performance
 * records) to the file and forwards it to the previously installed handler, or to stderr when
 * there was none; it stays installed for the rest of the process. When a QCoreApplication
 * exists, a `session.ending` event is recorded and the file flushed on `aboutToQuit`.
 *
 * Calling it again after a successful initialization does nothing and returns true.
 *
 * @param log_directory Target directory; empty selects `<AppLocalDataLocation>/logs`.
 * @return False when the directory or the log file cannot be created; the logger then stays
 *         uninitialized and a later call may retry.
 */
bool initialize(const QString& log_directory = {});

/** @brief Returns whether initialize() has succeeded and records are being written. */
bool is_initialized();

/** @brief Returns the active session log file path, or an empty string before initialize(). */
QString log_file_path();

/**
 * @brief Returns milliseconds elapsed since logger initialization.
 * @return Elapsed session time, or -1 before initialize() has succeeded.
 */
qint64 session_elapsed_ms();

/**
 * @brief Sets elapsed process time at the instant the logger was initialized.
 * @param elapsed_ms Milliseconds since process creation; negative values are clamped to 0.
 */
void set_process_start_offset_ms(qint64 elapsed_ms);

/**
 * @brief Returns milliseconds elapsed since operating-system process creation.
 *
 * Computed as the offset passed to set_process_start_offset_ms() plus session_elapsed_ms();
 * before initialize() only the offset (0 by default) is returned.
 */
qint64 process_elapsed_ms();

/** @brief Flushes pending log records to the session file; does nothing before initialize(). */
void flush();

/**
 * @brief Records a structured instantaneous event.
 *
 * Emits a compact JSON object (`kind`, `name`, `fields`) as an info message in the
 * `performance` logging category. Does nothing before initialize().
 *
 * @param name Event name, such as `session.started`.
 * @param fields Additional JSON-serializable fields.
 */
void record_event(const QString& name, const QVariantMap& fields = {});

/**
 * @brief Records a structured duration metric in milliseconds.
 *
 * Emits a compact JSON object (`kind`, `name`, `duration_ms`, `fields`) as an info message in
 * the `performance` logging category. Does nothing before initialize().
 *
 * @param name Metric name.
 * @param duration_ms Measured duration in milliseconds.
 * @param fields Additional JSON-serializable fields.
 */
void record_duration(const QString& name, qint64 duration_ms, const QVariantMap& fields = {});

/**
 * @brief Measures a scope and optional checkpoints using structured log records.
 *
 * A span instance is not synchronized and must be used from one thread at a time; the records
 * it emits go through the thread-safe record_duration().
 */
class ScopedSpan final {
public:
    /**
     * @brief Starts measuring a named span.
     * @param name Metric name used for the final record and as checkpoint prefix.
     * @param fields Fields included with every record emitted by this span.
     */
    explicit ScopedSpan(QString name, QVariantMap fields = {});

    /** @brief Records the completed span, including its `outcome` field, when it leaves scope. */
    ~ScopedSpan();

    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;
    ScopedSpan(ScopedSpan&&) = delete;
    ScopedSpan& operator=(ScopedSpan&&) = delete;

    /** @brief Adds or replaces a field included with the final span record. */
    void add_field(const QString& key, const QVariant& value);

    /** @brief Sets the outcome included with the final span record. */
    void set_outcome(const QString& outcome);

    /**
     * @brief Records time since the previous checkpoint and total span time.
     *
     * Emits a duration record named `<span name>.<checkpoint_name>` whose fields combine the
     * span fields, @p fields, `checkpoint` and `total_ms`.
     */
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
