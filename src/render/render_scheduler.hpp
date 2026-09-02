#pragma once

#include <QObject>
#include <QImage>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThreadPool>

#include <optional>
#include <functional>

struct RenderRequest {
    QString document_path;
    QString document_hash;
    int page_index = -1;
    QSize pixel_size;
    int rotation = 0;
    int generation = 0;
};

class RenderScheduler : public QObject {
    Q_OBJECT

public:
    using RenderFunction = std::function<QImage(const RenderRequest&, QString*)>;

    /** @brief Constructs an asynchronous render scheduler. */
    explicit RenderScheduler(
        QObject* parent = nullptr,
        RenderFunction render_function = {},
        int maximum_worker_count = 0);
    /** @brief Stops queued work and waits for active render workers. */
    ~RenderScheduler() override;

    /** @brief Cancels the current generation of render work. */
    void clear();
    /** @brief Cancels queued work and waits for active workers to finish. */
    void stop_and_wait();
    /** @brief Returns the current render generation. */
    int generation() const;
    /** @brief Queues @p request on the bounded render pool with @p priority. */
    void request_render(const RenderRequest& request, int priority);

signals:
    /** @brief Emitted immediately before a render starts. */
    void render_started(const RenderRequest& request);
    /** @brief Emitted when a render finishes or fails. */
    void render_finished(const RenderRequest& request, const QImage& image, qint64 elapsed_ms, const QString& error_message);

private:
    struct PendingJob {
        RenderRequest request;
        int priority = 0;
        qint64 sequence = 0;
        qint64 queued_at_ms = -1;
    };

    /** @brief Creates a stable identifier for a render request. */
    QString job_id_for_request(const RenderRequest& request) const;
    /** @brief Runs queued render jobs until no pending work remains. */
    void run_worker();
    /** @brief Removes and returns the highest-priority pending job. */
    std::optional<PendingJob> take_next_job();
    /** @brief Completes one active job and conditionally delivers its result. */
    void complete_job(
        const PendingJob& job,
        const QImage& image,
        qint64 elapsed_ms,
        const QString& error_message);

    mutable QMutex mutex_;
    QSet<QString> active_jobs_;
    QHash<QString, PendingJob> pending_jobs_;
    QThreadPool render_pool_;
    qint64 next_sequence_ = 0;
    int maximum_worker_count_ = 0;
    int worker_count_ = 0;
    int generation_ = 0;
    RenderFunction render_function_;
};
