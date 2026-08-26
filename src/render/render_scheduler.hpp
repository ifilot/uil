#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThreadPool>

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
    /** @brief Constructs an asynchronous render scheduler. */
    explicit RenderScheduler(QObject* parent = nullptr);
    /** @brief Stops queued work and waits for active render workers. */
    ~RenderScheduler() override;

    /** @brief Cancels the current generation of render work. */
    void clear();
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
    /** @brief Creates a stable identifier for a render request. */
    QString job_id_for_request(const RenderRequest& request) const;
    /** @brief Removes a completed job from the active-job set. */
    void remove_active_job(const QString& job_id);

    mutable QMutex mutex_;
    QSet<QString> active_jobs_;
    QThreadPool render_pool_;
    int generation_ = 0;
};
