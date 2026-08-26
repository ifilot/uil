#include "video_frame_buffer.hpp"

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
    QWaitCondition buffer_changed;
    QQueue<DecodedVideoFrame> frames;
    QThread* thread = nullptr;
    QString error_message;
    int target_buffer_ms = 3000;
    bool stopRequested = false;
    bool finished = false;
};

VideoFrameBuffer::VideoFrameBuffer(QObject* parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>()) {
}

VideoFrameBuffer::~VideoFrameBuffer() {
    stop();
}

void VideoFrameBuffer::start(const QString& path, int target_buffer_ms) {
    stop();

    {
        QMutexLocker locker(&impl_->mutex);
        impl_->target_buffer_ms = std::max(250, target_buffer_ms);
        impl_->stopRequested = false;
        impl_->finished = false;
        impl_->error_message.clear();
        impl_->frames.clear();
    }

    impl_->thread = QThread::create([this, path] {
        decode_loop(path);
    });
    impl_->thread->setObjectName(QStringLiteral("Video decode buffer"));
    impl_->thread->start();
}

void VideoFrameBuffer::stop() {
    QThread* thread = nullptr;
    {
        QMutexLocker locker(&impl_->mutex);
        impl_->stopRequested = true;
        impl_->buffer_changed.wakeAll();
        thread = impl_->thread;
        impl_->thread = nullptr;
    }

    if (thread) {
        thread->quit();
        thread->wait();
        delete thread;
    }

    {
        QMutexLocker locker(&impl_->mutex);
        impl_->frames.clear();
        impl_->finished = true;
    }
}

std::optional<DecodedVideoFrame> VideoFrameBuffer::take_frame() {
    std::optional<DecodedVideoFrame> frame;
    {
        QMutexLocker locker(&impl_->mutex);
        if (!impl_->frames.isEmpty()) {
            frame = impl_->frames.dequeue();
            impl_->buffer_changed.wakeAll();
        }
    }

    if (frame) {
        emit_buffer_changed_queued();
    }
    return frame;
}

bool VideoFrameBuffer::is_finished() const {
    QMutexLocker locker(&impl_->mutex);
    return impl_->finished && impl_->frames.isEmpty();
}

bool VideoFrameBuffer::has_frames() const {
    QMutexLocker locker(&impl_->mutex);
    return !impl_->frames.isEmpty();
}

int VideoFrameBuffer::buffered_duration_ms() const {
    QMutexLocker locker(&impl_->mutex);
    return buffered_duration_locked();
}

QString VideoFrameBuffer::error_message() const {
    QMutexLocker locker(&impl_->mutex);
    return impl_->error_message;
}

void VideoFrameBuffer::decode_loop(QString path) {
    VideoFrameReader reader;
    QString error_message;
    if (!reader.open(path, &error_message)) {
        {
            QMutexLocker locker(&impl_->mutex);
            impl_->error_message = error_message;
            impl_->finished = true;
        }
        emit_failed_queued(error_message);
        return;
    }

    while (true) {
        {
            QMutexLocker locker(&impl_->mutex);
            if (impl_->stopRequested) {
                return;
            }

            while (!impl_->stopRequested && buffered_duration_locked() >= impl_->target_buffer_ms) {
                impl_->buffer_changed.wait(&impl_->mutex, 50);
            }

            if (impl_->stopRequested) {
                return;
            }
        }

        QString frameError;
        std::optional<DecodedVideoFrame> frame = reader.read_next_frame(&frameError);
        if (!frame) {
            {
                QMutexLocker locker(&impl_->mutex);
                impl_->error_message = frameError;
                impl_->finished = true;
            }
            if (!frameError.isEmpty()) {
                emit_failed_queued(frameError);
            } else {
                emit_finished_queued();
            }
            return;
        }

        {
            QMutexLocker locker(&impl_->mutex);
            if (impl_->stopRequested) {
                return;
            }
            impl_->frames.enqueue(*frame);
        }

        emit_frame_available_queued();
        emit_buffer_changed_queued();
    }
}

int VideoFrameBuffer::buffered_duration_locked() const {
    if (impl_->frames.isEmpty()) {
        return 0;
    }

    if (impl_->frames.size() == 1) {
        return 33;
    }

    const qint64 firstPts = impl_->frames.head().pts_ms;
    const qint64 lastPts = impl_->frames.back().pts_ms;
    if (lastPts > firstPts) {
        return int(std::clamp<qint64>(lastPts - firstPts + 33, 0, 60'000));
    }

    return std::min(int(impl_->frames.size()) * 33, 60'000);
}

void VideoFrameBuffer::emit_frame_available_queued() {
    QMetaObject::invokeMethod(this, [this] {
        emit frame_available();
    }, Qt::QueuedConnection);
}

void VideoFrameBuffer::emit_finished_queued() {
    QMetaObject::invokeMethod(this, [this] {
        emit finished();
    }, Qt::QueuedConnection);
}

void VideoFrameBuffer::emit_failed_queued(const QString& error_message) {
    QMetaObject::invokeMethod(this, [this, error_message] {
        emit failed(error_message);
    }, Qt::QueuedConnection);
}

void VideoFrameBuffer::emit_buffer_changed_queued() {
    int durationMs = 0;
    int frame_count = 0;
    {
        QMutexLocker locker(&impl_->mutex);
        durationMs = buffered_duration_locked();
        frame_count = impl_->frames.size();
    }

    QMetaObject::invokeMethod(this, [this, durationMs, frame_count] {
        emit buffer_changed(durationMs, frame_count);
    }, Qt::QueuedConnection);
}
