#include "render_scheduler.hpp"

#include "pdf/qt_pdf_backend.hpp"
#include "util/performance_log.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <iterator>
#include <memory>

Q_LOGGING_CATEGORY(logRenderScheduler, "render.scheduler")

namespace {
struct WorkerPdfContext {
    std::unique_ptr<QtPdfBackend> backend;
    QString document_hash;
    QString document_path;
    int generation = -1;
};

}  // namespace

RenderScheduler::RenderScheduler(
    QObject* parent,
    RenderFunction render_function,
    int maximum_worker_count)
    : QObject(parent),
      render_function_(std::move(render_function)) {
    const int ideal_thread_count = QThread::idealThreadCount();
    maximum_worker_count_ = maximum_worker_count > 0
        ? std::clamp(maximum_worker_count, 1, 4)
        : std::clamp(ideal_thread_count > 0 ? ideal_thread_count / 2 : 2, 2, 4);
    render_pool_.setMaxThreadCount(maximum_worker_count_);
    render_pool_.setExpiryTimeout(30'000);
    performance_log::record_event(
        QStringLiteral("render.pool_configured"),
        {{QStringLiteral("max_thread_count"), maximum_worker_count_}});
}

RenderScheduler::~RenderScheduler() {
    stop_and_wait();
}

void RenderScheduler::clear() {
    {
        QMutexLocker locker(&mutex_);
        ++generation_;
        pending_jobs_.clear();
        active_jobs_.clear();
    }
}

void RenderScheduler::stop_and_wait() {
    clear();
    render_pool_.waitForDone();
}

int RenderScheduler::generation() const {
    QMutexLocker locker(&mutex_);
    return generation_;
}

void RenderScheduler::request_render(const RenderRequest& request, int priority) {
    if (request.document_path.isEmpty() || request.document_hash.isEmpty()
        || request.page_index < 0 || !request.pixel_size.isValid()) {
        return;
    }

    const QString job_id = job_id_for_request(request);
    bool start_worker = false;
    {
        QMutexLocker locker(&mutex_);
        if (request.generation != generation_ || active_jobs_.contains(job_id)) {
            return;
        }
        auto pending = pending_jobs_.find(job_id);
        if (pending != pending_jobs_.end()) {
            if (priority > pending->priority) {
                pending->priority = priority;
                performance_log::record_event(QStringLiteral("render.priority_promoted"), {
                    {QStringLiteral("page"), request.page_index + 1},
                    {QStringLiteral("priority"), priority}
                });
            }
            return;
        }

        pending_jobs_.insert(job_id, PendingJob{
            request,
            priority,
            next_sequence_++,
            performance_log::session_elapsed_ms()
        });
        if (worker_count_ < maximum_worker_count_) {
            ++worker_count_;
            start_worker = true;
        }
    }

    if (start_worker) {
        render_pool_.start(QRunnable::create([this] {
            run_worker();
        }));
    }
}

void RenderScheduler::run_worker() {
    WorkerPdfContext context;
    while (true) {
        const std::optional<PendingJob> next_job = take_next_job();
        if (!next_job) {
            return;
        }
        const PendingJob job = *next_job;
        const RenderRequest& request = job.request;
        const qint64 worker_started_at_ms = performance_log::session_elapsed_ms();
        const qint64 queue_wait_ms = job.queued_at_ms >= 0 && worker_started_at_ms >= job.queued_at_ms
            ? worker_started_at_ms - job.queued_at_ms
            : -1;
        QMetaObject::invokeMethod(this, [this, request] {
            emit render_started(request);
        }, Qt::QueuedConnection);
        QElapsedTimer timer;
        timer.start();

        QString error_message;
        QImage image;
        bool backend_reused = false;
        QElapsedTimer backend_timer;
        backend_timer.start();
        if (render_function_) {
            image = render_function_(request, &error_message);
        } else {
            backend_reused = context.backend
                && context.document_hash == request.document_hash
                && context.document_path == request.document_path
                && context.generation == request.generation;
            if (!backend_reused) {
                auto backend = std::make_unique<QtPdfBackend>();
                if (!backend->open(request.document_path, &error_message)) {
                    error_message = QStringLiteral("Could not open worker PDF: %1").arg(error_message);
                    context = WorkerPdfContext{};
                } else {
                    context.backend = std::move(backend);
                    context.document_hash = request.document_hash;
                    context.document_path = request.document_path;
                    context.generation = request.generation;
                }
            }
        }

        const qint64 backend_open_ms = backend_timer.elapsed();
        qint64 raster_ms = 0;
        if (!render_function_ && error_message.isEmpty() && context.backend) {
            QElapsedTimer raster_timer;
            raster_timer.start();
            image = context.backend->render_page(request.page_index, request.pixel_size);
            raster_ms = raster_timer.elapsed();
        }
        if (error_message.isEmpty() && image.isNull()) {
            error_message = QStringLiteral("Render failed");
        }

        const qint64 elapsed_ms = timer.elapsed();
        performance_log::record_duration(QStringLiteral("render.page_worker"), elapsed_ms, {
            {QStringLiteral("backend_open_ms"), backend_open_ms},
            {QStringLiteral("backend_reused"), backend_reused},
            {QStringLiteral("document"), QFileInfo(request.document_path).fileName()},
            {QStringLiteral("generation"), request.generation},
            {QStringLiteral("height"), request.pixel_size.height()},
            {QStringLiteral("outcome"),
             error_message.isEmpty() ? QStringLiteral("rendered") : QStringLiteral("failed")},
            {QStringLiteral("page"), request.page_index + 1},
            {QStringLiteral("queue_wait_ms"), queue_wait_ms},
            {QStringLiteral("raster_ms"), raster_ms},
            {QStringLiteral("width"), request.pixel_size.width()}
        });
        complete_job(job, image, elapsed_ms, error_message);
    }
}

QString RenderScheduler::job_id_for_request(const RenderRequest& request) const {
    return QStringLiteral("%1:%2:%3x%4:%5:%6")
        .arg(request.document_hash)
        .arg(request.page_index)
        .arg(request.pixel_size.width())
        .arg(request.pixel_size.height())
        .arg(request.rotation)
        .arg(request.generation);
}

std::optional<RenderScheduler::PendingJob> RenderScheduler::take_next_job() {
    QMutexLocker locker(&mutex_);
    if (pending_jobs_.isEmpty()) {
        --worker_count_;
        return std::nullopt;
    }

    auto selected = pending_jobs_.begin();
    for (auto it = std::next(selected); it != pending_jobs_.end(); ++it) {
        if (it->priority > selected->priority
            || (it->priority == selected->priority && it->sequence < selected->sequence)) {
            selected = it;
        }
    }

    PendingJob job = selected.value();
    active_jobs_.insert(selected.key());
    pending_jobs_.erase(selected);
    return job;
}

void RenderScheduler::complete_job(
    const PendingJob& job,
    const QImage& image,
    qint64 elapsed_ms,
    const QString& error_message) {
    const QString job_id = job_id_for_request(job.request);
    bool deliver_result = false;
    {
        QMutexLocker locker(&mutex_);
        active_jobs_.remove(job_id);
        deliver_result = job.request.generation == generation_;
    }
    if (!deliver_result) {
        return;
    }

    QMetaObject::invokeMethod(this, [this, request = job.request, image, elapsed_ms, error_message] {
        emit render_finished(request, image, elapsed_ms, error_message);
    }, Qt::QueuedConnection);
}
