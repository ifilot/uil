#pragma once

#include <QImage>
#include <QString>

#include <atomic>
#include <memory>
#include <optional>

/** @brief One decoded video frame converted to a detached Qt image. */
struct DecodedVideoFrame {
    /**
     * @brief Frame pixels.
     *
     * Opaque sources produce QImage::Format_RGB32 (alpha byte 0xFF), which Qt's raster painter
     * draws without blending. Sources whose pixel format carries alpha produce
     * QImage::Format_ARGB32_Premultiplied.
     */
    QImage image;
    /** @brief Presentation timestamp in milliseconds, or 0 when the stream has none. */
    qint64 pts_ms = 0;
};

/**
 * @brief Sequential FFmpeg-backed decoder for the best video stream of a media file.
 *
 * A reader is single-threaded: open(), read_next_frame(), close() and is_open() must not be
 * called concurrently. Only the flag registered with set_interrupt_flag() may be written from
 * another thread.
 */
class VideoFrameReader {
public:
    /** @brief Selects how FFmpeg spreads decoding work across its internal threads. */
    enum class Threading {
        /** Automatic thread count with frame and slice threading, for sequential playback. */
        Playback,
        /** Decodes on the calling thread only; cheapest when just one frame is needed. */
        SingleFrame
    };

    /** @brief Constructs an unopened video-frame reader. */
    VideoFrameReader();
    /** @brief Releases the decoder and its input resources. */
    ~VideoFrameReader();

    /** @brief Disables copying because the reader exclusively owns decoder state. */
    VideoFrameReader(const VideoFrameReader&) = delete;
    /** @brief Disables copy assignment because the reader exclusively owns decoder state. */
    VideoFrameReader& operator=(const VideoFrameReader&) = delete;

    /**
     * @brief Opens @p path for sequential video decoding.
     *
     * Any previously opened input is closed first.
     *
     * @param path Local media file path.
     * @param error_message Receives a readable reason on failure; may be null.
     * @param threading Decoder threading strategy; see Threading.
     * @return True when the input and its video decoder are ready.
     */
    bool open(const QString& path, QString* error_message = nullptr,
              Threading threading = Threading::Playback);
    /**
     * @brief Decodes and returns the next video frame in presentation order.
     *
     * @param error_message Cleared on entry; receives a message on failure; may be null.
     * @return The next frame. std::nullopt with an empty @p error_message means the end of the
     *         stream was reached; std::nullopt with a message means decoding failed (including
     *         when the reader is not open or the interrupt flag was raised).
     */
    std::optional<DecodedVideoFrame> read_next_frame(QString* error_message = nullptr);
    /** @brief Closes the current decoder input and releases its scaler and buffers. */
    void close();
    /** @brief Returns whether a decoder input is currently open. */
    bool is_open() const;
    /**
     * @brief Registers a flag that aborts blocking work when it becomes true.
     *
     * The flag is polled between demuxed packets and from FFmpeg's I/O interrupt callback, so
     * a pending open() or read_next_frame() returns promptly with an error once it is raised.
     * The flag may be written from any thread, but it must outlive every reader call made
     * while it is registered. Pass null to unregister.
     */
    void set_interrupt_flag(const std::atomic<bool>* interrupt_requested);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Extracts the first decodable frame from the video at @p path.
 *
 * Uses VideoFrameReader::Threading::SingleFrame, so no decoder thread pool is created.
 *
 * @param path Local media file path.
 * @param error_message Receives a readable reason when no frame is returned; may be null.
 * @return The first frame in the formats described by DecodedVideoFrame::image, or a null
 *         image on failure.
 */
QImage extract_first_video_frame(const QString& path, QString* error_message = nullptr);
