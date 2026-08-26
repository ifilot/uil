#pragma once

#include <QImage>
#include <QString>

#include <memory>
#include <optional>

struct DecodedVideoFrame {
    QImage image;
    qint64 pts_ms = 0;
};

class VideoFrameReader {
public:
    /** @brief Constructs an unopened video-frame reader. */
    VideoFrameReader();
    /** @brief Releases the decoder and its input resources. */
    ~VideoFrameReader();

    /** @brief Disables copying because the reader exclusively owns decoder state. */
    VideoFrameReader(const VideoFrameReader&) = delete;
    /** @brief Disables copy assignment because the reader exclusively owns decoder state. */
    VideoFrameReader& operator=(const VideoFrameReader&) = delete;

    /** @brief Opens @p path for sequential video decoding. */
    bool open(const QString& path, QString* error_message = nullptr);
    /** @brief Decodes and returns the next available video frame. */
    std::optional<DecodedVideoFrame> read_next_frame(QString* error_message = nullptr);
    /** @brief Closes the current decoder input. */
    void close();
    /** @brief Returns whether a decoder input is currently open. */
    bool is_open() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief Extracts the first decodable frame from the video at @p path. */
QImage extract_first_video_frame(const QString& path, QString* error_message = nullptr);
