#pragma once

#include "video_frame_extractor.hpp"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

/**
 * @brief Sequential source used by the asynchronous video buffer.
 *
 * Every method is called on the buffer's decoder thread, never concurrently.
 */
class VideoFrameSource {
public:
    /** @brief Releases the source on the decoder thread. */
    virtual ~VideoFrameSource() = default;
    /**
     * @brief Opens @p path for sequential decoding.
     *
     * @param error_message Receives a readable reason on failure; never null when called by
     *        VideoFrameBuffer.
     * @return True when read_next_frame() may be called.
     */
    virtual bool open(const QString& path, QString* error_message) = 0;
    /**
     * @brief Returns the next frame in presentation order.
     *
     * @param error_message Receives a message on failure; never null when called by
     *        VideoFrameBuffer.
     * @return The next frame; std::nullopt with an empty @p error_message for the end of the
     *         stream, or std::nullopt with a message for a failure.
     */
    virtual std::optional<DecodedVideoFrame> read_next_frame(QString* error_message) = 0;
    /**
     * @brief Registers a flag that VideoFrameBuffer::stop() raises from another thread.
     *
     * Called once before open(). Sources that can block for long should poll the flag and
     * return early with an error once it is true. The default implementation ignores it. The
     * flag outlives the source.
     */
    virtual void set_interrupt_flag(const std::atomic<bool>* /*interrupt_requested*/) {}
};

/**
 * @brief Decodes a video on a worker thread into a bounded queue of ready-to-paint frames.
 *
 * Threading contract:
 * - Construction, start(), stop() and destruction must happen on the thread that owns this
 *   object (normally the GUI thread). start(), stop() and the destructor block while they join
 *   the previous decoder thread. stop() raises an interrupt flag and wakes the decoder, so the
 *   wait is bounded by the source call in progress (for VideoFrameReader: one packet decode or
 *   one frame conversion, since the flag is polled between packets and during I/O) plus the
 *   source's teardown.
 * - take_frame() and the query methods lock an internal mutex and may be called from any
 *   thread.
 * - The FrameSourceFactory and every VideoFrameSource method run on the decoder thread; a new
 *   source is created for each start().
 * - Signals are queued to the thread this object lives in. frame_available() and
 *   buffer_changed() are coalesced: at most one of each is pending, so one emission can stand
 *   for several frames. Notifications from a run that has since been stopped or restarted are
 *   discarded.
 *
 * Buffering: the decoder runs ahead until the queue holds target_buffer_ms of media (clamped
 * to at least 250 ms), 180 frames, or 256 MiB of pixels, whichever comes first.
 */
class VideoFrameBuffer : public QObject {
    Q_OBJECT

public:
    /** @brief Creates a fresh frame source; invoked on the decoder thread for each start(). */
    using FrameSourceFactory = std::function<std::unique_ptr<VideoFrameSource>()>;

    /** @brief Constructs an empty buffer that decodes with VideoFrameReader. */
    explicit VideoFrameBuffer(QObject* parent = nullptr);
    /** @brief Constructs a buffer using an injectable decoder source factory. */
    VideoFrameBuffer(FrameSourceFactory source_factory, QObject* parent = nullptr);
    /** @brief Stops decoding (joining the decoder thread) and releases the buffer. */
    ~VideoFrameBuffer() override;

    /** @brief Disables copying because the buffer owns a decoder thread. */
    VideoFrameBuffer(const VideoFrameBuffer&) = delete;
    /** @brief Disables copy assignment because the buffer owns a decoder thread. */
    VideoFrameBuffer& operator=(const VideoFrameBuffer&) = delete;

    /**
     * @brief Stops any current run, then starts decoding @p path on a new decoder thread.
     *
     * @param path Media file passed to VideoFrameSource::open().
     * @param target_buffer_ms Playback duration to decode ahead; clamped to at least 250 ms.
     */
    void start(const QString& path, int target_buffer_ms = 3000);
    /**
     * @brief Stops the decoder, joins its thread and clears all buffered frames.
     *
     * Blocks until the decoder thread has exited. Afterwards is_finished() is true and pending
     * notifications from the stopped run are discarded. Safe to call when nothing is running.
     */
    void stop();
    /** @brief Removes and returns the oldest buffered frame, or std::nullopt when empty. */
    std::optional<DecodedVideoFrame> take_frame();
    /**
     * @brief Returns whether no more frames will become available.
     *
     * True only when decoding has ended (end of stream, failure, stop(), or never started) and
     * every buffered frame has been taken.
     */
    bool is_finished() const;
    /** @brief Returns whether at least one decoded frame is available. */
    bool has_frames() const;
    /** @brief Returns the approximate buffered playback duration in milliseconds. */
    int buffered_duration_ms() const;
    /** @brief Returns the error of the current run, or an empty string when none occurred. */
    QString error_message() const;

signals:
    /** @brief Emitted (coalesced) when decoded frames become available. */
    void frame_available();
    /** @brief Emitted once when the source reaches the end of the input; frames may remain. */
    void finished();
    /** @brief Emitted once when opening or decoding fails; earlier frames stay buffered. */
    void failed(const QString& error_message);
    /** @brief Emitted (coalesced) with the buffer state at delivery time when it changes. */
    void buffer_changed(int buffered_duration_ms, int frame_count);

private:
    /** @brief Runs the decoding loop for @p path on the worker thread for run @p generation. */
    void decode_loop(const QString& path, quint64 generation);
    /**
     * @brief Records the end of run @p generation and queues finished() or failed().
     *
     * Does nothing when a stop was requested, because the owner no longer wants the outcome.
     *
     * @param error_message Empty for the end of the stream; otherwise the failure reason.
     */
    void finish_decoding(quint64 generation, const QString& error_message);
    /** @brief Calculates buffered duration while the state lock is held. */
    int buffered_duration_locked() const;
    /** @brief Returns whether @p generation is still current; requires the state lock. */
    bool is_current_generation_locked(quint64 generation) const;
    /** @brief Queues a coalesced frame-available notification on this object's thread. */
    void emit_frame_available_queued(quint64 generation);
    /** @brief Queues a decoding-finished notification on this object's thread. */
    void emit_finished_queued(quint64 generation);
    /** @brief Queues a decoding-failed notification on this object's thread. */
    void emit_failed_queued(quint64 generation, const QString& error_message);
    /** @brief Queues a coalesced buffer-state notification on this object's thread. */
    void emit_buffer_changed_queued(quint64 generation);

    class Impl;
    std::unique_ptr<Impl> impl_;
};
