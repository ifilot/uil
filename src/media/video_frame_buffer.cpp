#include "video_frame_buffer.hpp"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <utility>

namespace {
constexpr int kMinimumTargetBufferMs = 250;
constexpr int kMaximumBufferedFrames = 180;
constexpr qint64 kMaximumBufferedBytes = 256LL * 1024LL * 1024LL;
/** Upper bound for a decoder wait; stop() and take_frame() normally wake it much sooner. */
constexpr unsigned long kDecoderWaitTimeoutMs = 50;

/** @brief Returns the detached pixel storage occupied by a decoded frame. */
qint64 frame_bytes(const DecodedVideoFrame& frame) { return qint64(frame.image.sizeInBytes()); }

/** @brief Adapts VideoFrameReader to the buffer's source interface. */
class VideoFrameReaderSource final : public VideoFrameSource {
public:
    /** @brief Opens @p path for frame- and slice-threaded playback decoding. */
    bool open(const QString& path, QString* error_message) override {
        return reader_.open(path, error_message, VideoFrameReader::Threading::Playback);
    }

    /** @brief Decodes the next frame; see VideoFrameReader::read_next_frame(). */
    std::optional<DecodedVideoFrame> read_next_frame(QString* error_message) override {
        return reader_.read_next_frame(error_message);
    }

    /** @brief Forwards the stop flag so demuxing and decoding abort promptly. */
    void set_interrupt_flag(const std::atomic<bool>* interrupt_requested) override {
        reader_.set_interrupt_flag(interrupt_requested);
    }

private:
    VideoFrameReader reader_;
};
}  // namespace

Q_LOGGING_CATEGORY(log_video_buffer, "video.buffer")

class VideoFrameBuffer::Impl {
public:
    FrameSourceFactory source_factory;
    mutable QMutex mutex;
    QWaitCondition buffer_changed;
    QQueue<DecodedVideoFrame> frames;
    QThread* thread = nullptr;
    QString error_message;
    /** Raised by stop() without the mutex so a blocked source can poll it. */
    std::atomic<bool> interrupt_requested{false};
    /** Identifies the current run; queued notifications from other runs are dropped. */
    quint64 generation = 0;
    int target_buffer_ms = 3000;
    qint64 buffered_bytes = 0;
    bool stop_requested = false;
    bool finished = true;
    bool frame_notification_pending = false;
    bool buffer_notification_pending = false;
};

VideoFrameBuffer::VideoFrameBuffer(QObject* parent)
    : VideoFrameBuffer([] { return std::make_unique<VideoFrameReaderSource>(); }, parent) {}

VideoFrameBuffer::VideoFrameBuffer(FrameSourceFactory source_factory, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->source_factory = std::move(source_factory);
}

VideoFrameBuffer::~VideoFrameBuffer() { stop(); }

void VideoFrameBuffer::start(const QString& path, int target_buffer_ms) {
    stop();

    quint64 generation = 0;
    {
        QMutexLocker locker(&impl_->mutex);
        generation = ++impl_->generation;
        impl_->target_buffer_ms = std::max(kMinimumTargetBufferMs, target_buffer_ms);
        impl_->stop_requested = false;
        impl_->interrupt_requested.store(false, std::memory_order_release);
        impl_->finished = false;
        impl_->error_message.clear();
        impl_->frames.clear();
        impl_->buffered_bytes = 0;
        impl_->frame_notification_pending = false;
        impl_->buffer_notification_pending = false;
    }

    impl_->thread = QThread::create([this, path, generation] { decode_loop(path, generation); });
    impl_->thread->setObjectName(QStringLiteral("Video decode buffer"));
    impl_->thread->start();
}

void VideoFrameBuffer::stop() {
    QThread* thread = nullptr;
    {
        QMutexLocker locker(&impl_->mutex);
        impl_->stop_requested = true;
        // Interrupt a source blocked in demuxing/decoding, and wake a decoder waiting for room.
        impl_->interrupt_requested.store(true, std::memory_order_release);
        ++impl_->generation;
        impl_->buffer_changed.wakeAll();
        thread = impl_->thread;
        impl_->thread = nullptr;
    }

    if (thread) {
        thread->wait();
        delete thread;
    }

    {
        QMutexLocker locker(&impl_->mutex);
        impl_->frames.clear();
        impl_->buffered_bytes = 0;
        impl_->finished = true;
    }
}

std::optional<DecodedVideoFrame> VideoFrameBuffer::take_frame() {
    std::optional<DecodedVideoFrame> frame;
    quint64 generation = 0;
    {
        QMutexLocker locker(&impl_->mutex);
        if (!impl_->frames.isEmpty()) {
            frame = impl_->frames.dequeue();
            impl_->buffered_bytes -= frame_bytes(*frame);
            impl_->buffer_changed.wakeAll();
        }
        generation = impl_->generation;
    }

    if (frame) {
        emit_buffer_changed_queued(generation);
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

void VideoFrameBuffer::decode_loop(const QString& path, quint64 generation) {
    std::unique_ptr<VideoFrameSource> source =
        impl_->source_factory ? impl_->source_factory() : nullptr;
    if (!source) {
        finish_decoding(generation, QStringLiteral("No video decoder is available"));
        return;
    }

    source->set_interrupt_flag(&impl_->interrupt_requested);
    QString open_error;
    if (!source->open(path, &open_error)) {
        finish_decoding(generation,
                        open_error.isEmpty() ? QStringLiteral("Could not open video") : open_error);
        return;
    }

    while (true) {
        {
            QMutexLocker locker(&impl_->mutex);
            while (!impl_->stop_requested &&
                   (buffered_duration_locked() >= impl_->target_buffer_ms ||
                    impl_->frames.size() >= kMaximumBufferedFrames ||
                    impl_->buffered_bytes >= kMaximumBufferedBytes)) {
                impl_->buffer_changed.wait(&impl_->mutex, kDecoderWaitTimeoutMs);
            }

            if (impl_->stop_requested) {
                return;
            }
        }

        QString frame_error;
        std::optional<DecodedVideoFrame> frame = source->read_next_frame(&frame_error);
        if (!frame) {
            finish_decoding(generation, frame_error);
            return;
        }

        const qint64 decoded_frame_bytes = frame_bytes(*frame);
        if (decoded_frame_bytes <= 0 || decoded_frame_bytes > kMaximumBufferedBytes) {
            finish_decoding(generation,
                            QStringLiteral("Decoded video frame exceeds the buffer memory limit"));
            return;
        }

        {
            QMutexLocker locker(&impl_->mutex);
            while (!impl_->stop_requested && !impl_->frames.isEmpty() &&
                   (impl_->frames.size() >= kMaximumBufferedFrames ||
                    impl_->buffered_bytes + decoded_frame_bytes > kMaximumBufferedBytes)) {
                impl_->buffer_changed.wait(&impl_->mutex, kDecoderWaitTimeoutMs);
            }
            if (impl_->stop_requested) {
                return;
            }
            impl_->buffered_bytes += decoded_frame_bytes;
            impl_->frames.enqueue(std::move(*frame));
        }

        emit_frame_available_queued(generation);
        emit_buffer_changed_queued(generation);
    }
}

void VideoFrameBuffer::finish_decoding(quint64 generation, const QString& error_message) {
    {
        QMutexLocker locker(&impl_->mutex);
        if (impl_->stop_requested) {
            return;
        }
        impl_->error_message = error_message;
        impl_->finished = true;
    }

    if (error_message.isEmpty()) {
        emit_finished_queued(generation);
    } else {
        qCInfo(log_video_buffer) << "Video decoding failed:" << error_message;
        emit_failed_queued(generation, error_message);
    }
}

int VideoFrameBuffer::buffered_duration_locked() const {
    if (impl_->frames.isEmpty()) {
        return 0;
    }

    if (impl_->frames.size() == 1) {
        return 33;
    }

    const qint64 first_pts = impl_->frames.head().pts_ms;
    const qint64 last_pts = impl_->frames.back().pts_ms;
    if (last_pts > first_pts) {
        return int(std::clamp<qint64>(last_pts - first_pts + 33, 0, 60'000));
    }

    return std::min(int(impl_->frames.size()) * 33, 60'000);
}

bool VideoFrameBuffer::is_current_generation_locked(quint64 generation) const {
    return impl_->generation == generation;
}

void VideoFrameBuffer::emit_frame_available_queued(quint64 generation) {
    {
        QMutexLocker locker(&impl_->mutex);
        if (impl_->frame_notification_pending || !is_current_generation_locked(generation)) {
            return;
        }
        impl_->frame_notification_pending = true;
    }

    QMetaObject::invokeMethod(
        this,
        [this, generation] {
            {
                QMutexLocker locker(&impl_->mutex);
                if (!is_current_generation_locked(generation)) {
                    return;
                }
                impl_->frame_notification_pending = false;
            }
            emit frame_available();
        },
        Qt::QueuedConnection);
}

void VideoFrameBuffer::emit_finished_queued(quint64 generation) {
    QMetaObject::invokeMethod(
        this,
        [this, generation] {
            {
                QMutexLocker locker(&impl_->mutex);
                if (!is_current_generation_locked(generation)) {
                    return;
                }
            }
            emit finished();
        },
        Qt::QueuedConnection);
}

void VideoFrameBuffer::emit_failed_queued(quint64 generation, const QString& error_message) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, error_message] {
            {
                QMutexLocker locker(&impl_->mutex);
                if (!is_current_generation_locked(generation)) {
                    return;
                }
            }
            emit failed(error_message);
        },
        Qt::QueuedConnection);
}

void VideoFrameBuffer::emit_buffer_changed_queued(quint64 generation) {
    {
        QMutexLocker locker(&impl_->mutex);
        if (impl_->buffer_notification_pending || !is_current_generation_locked(generation)) {
            return;
        }
        impl_->buffer_notification_pending = true;
    }

    QMetaObject::invokeMethod(
        this,
        [this, generation] {
            int duration_ms = 0;
            int frame_count = 0;
            {
                QMutexLocker locker(&impl_->mutex);
                if (!is_current_generation_locked(generation)) {
                    return;
                }
                impl_->buffer_notification_pending = false;
                duration_ms = buffered_duration_locked();
                frame_count = impl_->frames.size();
            }
            emit buffer_changed(duration_ms, frame_count);
        },
        Qt::QueuedConnection);
}
