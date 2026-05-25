#pragma once

#include "VideoFrameExtractor.hpp"

#include <QObject>

#include <memory>
#include <optional>

class VideoFrameBuffer : public QObject {
    Q_OBJECT

public:
    explicit VideoFrameBuffer(QObject* parent = nullptr);
    ~VideoFrameBuffer() override;

    VideoFrameBuffer(const VideoFrameBuffer&) = delete;
    VideoFrameBuffer& operator=(const VideoFrameBuffer&) = delete;

    void start(const QString& path, int targetBufferMs = 3000);
    void stop();
    std::optional<DecodedVideoFrame> takeFrame();
    bool isFinished() const;
    bool hasFrames() const;
    int bufferedDurationMs() const;
    QString errorMessage() const;

signals:
    void frameAvailable();
    void finished();
    void failed(const QString& errorMessage);
    void bufferChanged(int bufferedDurationMs, int frameCount);

private:
    void decodeLoop(QString path);
    int bufferedDurationLocked() const;
    void emitFrameAvailableQueued();
    void emitFinishedQueued();
    void emitFailedQueued(const QString& errorMessage);
    void emitBufferChangedQueued();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
