#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSize>
#include <QString>

struct RenderRequest {
    QString documentPath;
    QString documentHash;
    int pageIndex = -1;
    QSize pixelSize;
    int rotation = 0;
    int generation = 0;
};

class RenderScheduler : public QObject {
    Q_OBJECT

public:
    explicit RenderScheduler(QObject* parent = nullptr);

    void clear();
    int generation() const;
    void requestRender(const RenderRequest& request, int priority);

signals:
    void renderStarted(const RenderRequest& request);
    void renderFinished(const RenderRequest& request, const QImage& image, qint64 elapsedMs, const QString& errorMessage);

private:
    QString jobIdForRequest(const RenderRequest& request) const;
    void removeActiveJob(const QString& jobId);

    mutable QMutex m_mutex;
    QSet<QString> m_activeJobs;
    int m_generation = 0;
};
