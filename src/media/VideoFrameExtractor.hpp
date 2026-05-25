#pragma once

#include <QImage>
#include <QString>

#include <memory>
#include <optional>

struct DecodedVideoFrame {
    QImage image;
    qint64 ptsMs = 0;
};

class VideoFrameReader {
public:
    VideoFrameReader();
    ~VideoFrameReader();

    VideoFrameReader(const VideoFrameReader&) = delete;
    VideoFrameReader& operator=(const VideoFrameReader&) = delete;

    bool open(const QString& path, QString* errorMessage = nullptr);
    std::optional<DecodedVideoFrame> readNextFrame(QString* errorMessage = nullptr);
    void close();
    bool isOpen() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

QImage extractFirstVideoFrame(const QString& path, QString* errorMessage = nullptr);
