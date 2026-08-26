#pragma once

#include "video_frame_extractor.hpp"

#include <QObject>

#include <memory>
#include <optional>

class VideoFrameBuffer : public QObject {
    Q_OBJECT

public:
    /** @brief Constructs an empty asynchronous frame buffer. */
    explicit VideoFrameBuffer(QObject* parent = nullptr);
    /** @brief Stops decoding and releases the buffer. */
    ~VideoFrameBuffer() override;

    /** @brief Disables copying because the buffer owns a decoder thread. */
    VideoFrameBuffer(const VideoFrameBuffer&) = delete;
    /** @brief Disables copy assignment because the buffer owns a decoder thread. */
    VideoFrameBuffer& operator=(const VideoFrameBuffer&) = delete;

    /** @brief Starts decoding @p path into a bounded playback buffer. */
    void start(const QString& path, int target_buffer_ms = 3000);
    /** @brief Stops the decoder and clears all buffered frames. */
    void stop();
    /** @brief Removes and returns the oldest buffered frame. */
    std::optional<DecodedVideoFrame> take_frame();
    /** @brief Returns whether decoding reached the end of the input. */
    bool is_finished() const;
    /** @brief Returns whether at least one decoded frame is available. */
    bool has_frames() const;
    /** @brief Returns the approximate buffered playback duration in milliseconds. */
    int buffered_duration_ms() const;
    /** @brief Returns the most recent decoder error message. */
    QString error_message() const;

signals:
    /** @brief Emitted when a decoded frame becomes available. */
    void frame_available();
    /** @brief Emitted when the decoder reaches the end of the input. */
    void finished();
    /** @brief Emitted when decoding fails. */
    void failed(const QString& error_message);
    /** @brief Emitted when the amount of buffered media changes. */
    void buffer_changed(int buffered_duration_ms, int frame_count);

private:
    /** @brief Runs the decoding loop for @p path on the worker thread. */
    void decode_loop(QString path);
    /** @brief Calculates buffered duration while the state lock is held. */
    int buffered_duration_locked() const;
    /** @brief Queues a frame-available notification on this object's thread. */
    void emit_frame_available_queued();
    /** @brief Queues a decoding-finished notification on this object's thread. */
    void emit_finished_queued();
    /** @brief Queues a decoding-failed notification on this object's thread. */
    void emit_failed_queued(const QString& error_message);
    /** @brief Queues a buffer-state notification on this object's thread. */
    void emit_buffer_changed_queued();

    class Impl;
    std::unique_ptr<Impl> impl_;
};
