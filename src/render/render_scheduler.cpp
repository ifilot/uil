#include "render_scheduler.hpp"

#include "pdf/qt_pdf_backend.hpp"
#include "util/performance_log.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <memory>

Q_LOGGING_CATEGORY(logRenderScheduler, "render.scheduler")

namespace {
struct WorkerPdfContext {
    std::unique_ptr<QtPdfBackend> backend;
    QString document_hash;
    QString document_path;
    int generation = -1;
};

/** @brief Returns the PDF backend retained by the current render worker. */
WorkerPdfContext& worker_pdf_context() {
    thread_local WorkerPdfContext context;
    return context;
}
}  // namespace

RenderScheduler::RenderScheduler(QObject* parent)
    : QObject(parent) {
    const int ideal_thread_count = QThread::idealThreadCount();
    const int render_thread_count = std::clamp(
        ideal_thread_count > 0 ? ideal_thread_count / 2 : 2,
        2,
        4);
    render_pool_.setMaxThreadCount(render_thread_count);
    render_pool_.setExpiryTimeout(30'000);
    performance_log::record_event(
        QStringLiteral("render.pool_configured"),
        {{QStringLiteral("max_thread_count"), render_thread_count}});
}

RenderScheduler::~RenderScheduler() {
    render_pool_.clear();
    render_pool_.waitForDone();
}

void RenderScheduler::clear() {
    {
        QMutexLocker locker(&mutex_);
        ++generation_;
        active_jobs_.clear();
    }
    render_pool_.clear();
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
    {
        QMutexLocker locker(&mutex_);
        if (request.generation != generation_ || active_jobs_.contains(job_id)) {
            return;
        }
        active_jobs_.insert(job_id);
    }

    emit render_started(request);
    QPointer<RenderScheduler> self(this);
    const qint64 queued_at_ms = performance_log::session_elapsed_ms();

    auto* runnable = QRunnable::create([self, request, job_id, queued_at_ms] {
        const qint64 worker_started_at_ms = performance_log::session_elapsed_ms();
        const qint64 queue_wait_ms = queued_at_ms >= 0 && worker_started_at_ms >= queued_at_ms
            ? worker_started_at_ms - queued_at_ms
            : -1;
        QElapsedTimer timer;
        timer.start();

        QString error_message;
        QImage image;
        WorkerPdfContext& context = worker_pdf_context();
        const bool backend_reused = context.backend
            && context.document_hash == request.document_hash
            && context.document_path == request.document_path
            && context.generation == request.generation;
        QElapsedTimer backend_timer;
        backend_timer.start();
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

        const qint64 backend_open_ms = backend_timer.elapsed();
        qint64 raster_ms = 0;
        if (error_message.isEmpty() && context.backend) {
            QElapsedTimer raster_timer;
            raster_timer.start();
            image = context.backend->render_page(request.page_index, request.pixel_size);
            raster_ms = raster_timer.elapsed();
            if (image.isNull()) {
                error_message = QStringLiteral("Render failed");
            }
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
        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, request, image, elapsed_ms, error_message, job_id] {
            if (!self) {
                return;
            }

            self->remove_active_job(job_id);
            emit self->render_finished(request, image, elapsed_ms, error_message);
        }, Qt::QueuedConnection);
    });

    render_pool_.start(runnable, priority);
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

void RenderScheduler::remove_active_job(const QString& job_id) {
    QMutexLocker locker(&mutex_);
    active_jobs_.remove(job_id);
}
