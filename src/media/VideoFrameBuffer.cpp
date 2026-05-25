#include "VideoFrameBuffer.hpp"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>

Q_LOGGING_CATEGORY(logVideoBuffer, "video.buffer")

class VideoFrameBuffer::Impl {
public:
    mutable QMutex mutex;
    QWaitCondition bufferChanged;
    QQueue<DecodedVideoFrame> frames;
    QThread* thread = nullptr;
    QString errorMessage;
    int targetBufferMs = 3000;
    bool stopRequested = false;
    bool finished = false;
};

VideoFrameBuffer::VideoFrameBuffer(QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>()) {
}

VideoFrameBuffer::~VideoFrameBuffer() {
    stop();
}

void VideoFrameBuffer::start(const QString& path, int targetBufferMs) {
    stop();

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->targetBufferMs = std::max(250, targetBufferMs);
        m_impl->stopRequested = false;
        m_impl->finished = false;
        m_impl->errorMessage.clear();
        m_impl->frames.clear();
    }

    m_impl->thread = QThread::create([this, path] {
        decodeLoop(path);
    });
    m_impl->thread->setObjectName(QStringLiteral("Video decode buffer"));
    m_impl->thread->start();
}

void VideoFrameBuffer::stop() {
    QThread* thread = nullptr;
    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->stopRequested = true;
        m_impl->bufferChanged.wakeAll();
        thread = m_impl->thread;
        m_impl->thread = nullptr;
    }

    if (thread) {
        thread->quit();
        thread->wait();
        delete thread;
    }

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->frames.clear();
        m_impl->finished = true;
    }
}

std::optional<DecodedVideoFrame> VideoFrameBuffer::takeFrame() {
    std::optional<DecodedVideoFrame> frame;
    {
        QMutexLocker locker(&m_impl->mutex);
        if (!m_impl->frames.isEmpty()) {
            frame = m_impl->frames.dequeue();
            m_impl->bufferChanged.wakeAll();
        }
    }

    if (frame) {
        emitBufferChangedQueued();
    }
    return frame;
}

bool VideoFrameBuffer::isFinished() const {
    QMutexLocker locker(&m_impl->mutex);
    return m_impl->finished && m_impl->frames.isEmpty();
}

bool VideoFrameBuffer::hasFrames() const {
    QMutexLocker locker(&m_impl->mutex);
    return !m_impl->frames.isEmpty();
}

int VideoFrameBuffer::bufferedDurationMs() const {
    QMutexLocker locker(&m_impl->mutex);
    return bufferedDurationLocked();
}

QString VideoFrameBuffer::errorMessage() const {
    QMutexLocker locker(&m_impl->mutex);
    return m_impl->errorMessage;
}

void VideoFrameBuffer::decodeLoop(QString path) {
    VideoFrameReader reader;
    QString errorMessage;
    if (!reader.open(path, &errorMessage)) {
        {
            QMutexLocker locker(&m_impl->mutex);
            m_impl->errorMessage = errorMessage;
            m_impl->finished = true;
        }
        emitFailedQueued(errorMessage);
        return;
    }

    while (true) {
        {
            QMutexLocker locker(&m_impl->mutex);
            if (m_impl->stopRequested) {
                return;
            }

            while (!m_impl->stopRequested && bufferedDurationLocked() >= m_impl->targetBufferMs) {
                m_impl->bufferChanged.wait(&m_impl->mutex, 50);
            }

            if (m_impl->stopRequested) {
                return;
            }
        }

        QString frameError;
        std::optional<DecodedVideoFrame> frame = reader.readNextFrame(&frameError);
        if (!frame) {
            {
                QMutexLocker locker(&m_impl->mutex);
                m_impl->errorMessage = frameError;
                m_impl->finished = true;
            }
            if (!frameError.isEmpty()) {
                emitFailedQueued(frameError);
            } else {
                emitFinishedQueued();
            }
            return;
        }

        {
            QMutexLocker locker(&m_impl->mutex);
            if (m_impl->stopRequested) {
                return;
            }
            m_impl->frames.enqueue(*frame);
        }

        emitFrameAvailableQueued();
        emitBufferChangedQueued();
    }
}

int VideoFrameBuffer::bufferedDurationLocked() const {
    if (m_impl->frames.isEmpty()) {
        return 0;
    }

    if (m_impl->frames.size() == 1) {
        return 33;
    }

    const qint64 firstPts = m_impl->frames.head().ptsMs;
    const qint64 lastPts = m_impl->frames.back().ptsMs;
    if (lastPts > firstPts) {
        return int(std::clamp<qint64>(lastPts - firstPts + 33, 0, 60'000));
    }

    return std::min(int(m_impl->frames.size()) * 33, 60'000);
}

void VideoFrameBuffer::emitFrameAvailableQueued() {
    QMetaObject::invokeMethod(this, [this] {
        emit frameAvailable();
    }, Qt::QueuedConnection);
}

void VideoFrameBuffer::emitFinishedQueued() {
    QMetaObject::invokeMethod(this, [this] {
        emit finished();
    }, Qt::QueuedConnection);
}

void VideoFrameBuffer::emitFailedQueued(const QString& errorMessage) {
    QMetaObject::invokeMethod(this, [this, errorMessage] {
        emit failed(errorMessage);
    }, Qt::QueuedConnection);
}

void VideoFrameBuffer::emitBufferChangedQueued() {
    int durationMs = 0;
    int frameCount = 0;
    {
        QMutexLocker locker(&m_impl->mutex);
        durationMs = bufferedDurationLocked();
        frameCount = m_impl->frames.size();
    }

    QMetaObject::invokeMethod(this, [this, durationMs, frameCount] {
        emit bufferChanged(durationMs, frameCount);
    }, Qt::QueuedConnection);
}
