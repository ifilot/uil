#include "RenderScheduler.hpp"

#include "pdf/QtPdfBackend.hpp"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

Q_LOGGING_CATEGORY(logRenderScheduler, "render.scheduler")

RenderScheduler::RenderScheduler(QObject* parent)
    : QObject(parent) {
}

void RenderScheduler::clear() {
    QMutexLocker locker(&m_mutex);
    ++m_generation;
    m_activeJobs.clear();
}

int RenderScheduler::generation() const {
    QMutexLocker locker(&m_mutex);
    return m_generation;
}

void RenderScheduler::requestRender(const RenderRequest& request, int priority) {
    if (request.documentPath.isEmpty() || request.documentHash.isEmpty()
        || request.pageIndex < 0 || !request.pixelSize.isValid()) {
        return;
    }

    const QString jobId = jobIdForRequest(request);
    {
        QMutexLocker locker(&m_mutex);
        if (request.generation != m_generation || m_activeJobs.contains(jobId)) {
            return;
        }
        m_activeJobs.insert(jobId);
    }

    emit renderStarted(request);
    QPointer<RenderScheduler> self(this);

    auto* runnable = QRunnable::create([self, request, jobId] {
        QElapsedTimer timer;
        timer.start();

        QString errorMessage;
        QtPdfBackend backend;
        QImage image;
        if (!backend.open(request.documentPath, &errorMessage)) {
            errorMessage = QStringLiteral("Could not open worker PDF: %1").arg(errorMessage);
        } else {
            image = backend.renderPage(request.pageIndex, request.pixelSize);
            if (image.isNull()) {
                errorMessage = QStringLiteral("Render failed");
            }
        }

        const qint64 elapsedMs = timer.elapsed();
        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, request, image, elapsedMs, errorMessage, jobId] {
            if (!self) {
                return;
            }

            self->removeActiveJob(jobId);
            emit self->renderFinished(request, image, elapsedMs, errorMessage);
        }, Qt::QueuedConnection);
    });

    QThreadPool::globalInstance()->start(runnable, priority);
}

QString RenderScheduler::jobIdForRequest(const RenderRequest& request) const {
    return QStringLiteral("%1:%2:%3x%4:%5:%6")
        .arg(request.documentHash)
        .arg(request.pageIndex)
        .arg(request.pixelSize.width())
        .arg(request.pixelSize.height())
        .arg(request.rotation)
        .arg(request.generation);
}

void RenderScheduler::removeActiveJob(const QString& jobId) {
    QMutexLocker locker(&m_mutex);
    m_activeJobs.remove(jobId);
}
