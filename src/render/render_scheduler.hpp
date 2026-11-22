#pragma once

#include <QByteArray>
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

/** @brief Describes one page rasterization job. */
struct RenderRequest {
    /** @brief Source file; used for logging and as a fallback when @c document_data is empty. */
    QString document_path;
    /** @brief Identity of the document version; part of the job and cache identity. */
    QString document_hash;
    int page_index = -1;
    /** @brief Exact output size in device pixels. */
    QSize pixel_size;
    int rotation = 0;
    /** @brief Scheduler generation the request was created for; see RenderScheduler::generation(). */
    int generation = 0;
    /**
     * @brief Implicitly shared PDF bytes captured when the document was opened.
     *
     * When set, workers render from these bytes instead of reopening @c document_path, so a
     * file rewritten on disk (for example by a LaTeX rebuild) cannot mix into the open deck.
     */
    QByteArray document_data;
};

/**
 * @brief Prioritized, coalescing page renderer backed by a small worker pool.
 *
 * Public methods are thread-safe, but signals are queued to the scheduler's own thread, so
 * the owner normally lives on the GUI thread. Each worker keeps its own PdfBackend.
 */
class RenderScheduler : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Optional replacement renderer, mainly for tests.
     *
     * It may run concurrently on several pool threads and must therefore be reentrant. It
     * returns the rendered image, or a null image and a message through its second argument.
     */
    using RenderFunction = std::function<QImage(const RenderRequest&, QString*)>;
    /** @brief Predicate deciding whether a pending request stays queued. */
    using PendingFilter = std::function<bool(const RenderRequest&)>;

    /**
     * @brief Constructs an asynchronous render scheduler.
     * @param[in] parent Owning QObject.
     * @param[in] render_function Renderer override; empty uses QtPdfBackend.
     * @param[in] maximum_worker_count Worker limit clamped to [1, 4]; 0 selects the default:
     *            one worker for QtPdfBackend, whose PDFium calls are serialized by a
     *            process-wide lock anyway, otherwise half the ideal thread count within [2, 4].
     */
    explicit RenderScheduler(
        QObject* parent = nullptr,
        RenderFunction render_function = {},
        int maximum_worker_count = 0);
    /** @brief Stops queued work and waits for active render workers. */
    ~RenderScheduler() override;

    /**
     * @brief Starts a new generation and drops all pending work.
     *
     * Renders already running are not interrupted, but their results are discarded and
     * render_finished() is not emitted for them.
     */
    void clear();
    /** @brief Calls clear() and blocks until every worker has returned. */
    void stop_and_wait();
    /** @brief Returns the current render generation; requests must carry this value. */
    int generation() const;
    /**
     * @brief Queues @p request with @p priority; higher priorities run first, ties are FIFO.
     *
     * Invalid requests and requests whose generation differs from generation() are ignored
     * silently. A request identical to a pending one only raises that job's priority; one
     * identical to a running job is dropped.
     */
    void request_render(const RenderRequest& request, int priority);
    /**
     * @brief Drops pending (not yet started) requests for which @p keep returns false.
     *
     * @p keep runs on the calling thread while the scheduler's lock is held, so it must be
     * quick and must not call back into the scheduler.
     * @return Number of requests removed.
     */
    int retain_pending(const PendingFilter& keep);

signals:
    /** @brief Emitted on the scheduler's thread shortly after a render starts. */
    void render_started(const RenderRequest& request);
    /**
     * @brief Emitted when a render of the current generation finishes or fails.
     *
     * On failure @p image is null and @p error_message is non-empty. Results of stale
     * generations are dropped without emitting this signal.
     */
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
