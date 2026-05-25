#include "VideoFrameExtractor.hpp"

#include <QByteArray>
#include <QFile>
#include <QLoggingCategory>

#include <array>

Q_LOGGING_CATEGORY(logVideo, "video")

#if defined(UIL_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {
struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const {
        if (context) {
            avformat_close_input(&context);
        }
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const {
        avcodec_free_context(&context);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const {
        av_frame_free(&frame);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const {
        av_packet_free(&packet);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* context) const {
        sws_freeContext(context);
    }
};

QString avErrorToString(int errorCode) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(errorCode, buffer.data(), buffer.size());
    return QString::fromLocal8Bit(buffer.data());
}

QImage frameToImage(const AVFrame* frame, AVCodecContext* codecContext) {
    std::unique_ptr<SwsContext, SwsContextDeleter> swsContext(
        sws_getContext(
            frame->width,
            frame->height,
            AVPixelFormat(frame->format),
            frame->width,
            frame->height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr));

    if (!swsContext) {
        return {};
    }

    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
    if (image.isNull()) {
        return {};
    }

    uint8_t* destinationData[4] = {image.bits(), nullptr, nullptr, nullptr};
    int destinationLinesize[4] = {int(image.bytesPerLine()), 0, 0, 0};
    const int convertedHeight = sws_scale(
        swsContext.get(),
        frame->data,
        frame->linesize,
        0,
        codecContext->height,
        destinationData,
        destinationLinesize);

    if (convertedHeight <= 0) {
        return {};
    }

    return image;
}

qint64 frameTimestampMs(const AVFrame* frame, const AVStream* stream) {
    const int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) {
        return 0;
    }
    return qint64(double(timestamp) * av_q2d(stream->time_base) * 1000.0);
}
}

class VideoFrameReader::Impl {
public:
    bool open(const QString& path, QString* errorMessage);
    std::optional<DecodedVideoFrame> readNextFrame(QString* errorMessage);
    void close();
    bool isOpen() const;

private:
    std::optional<DecodedVideoFrame> receiveFrame(QString* errorMessage);

    QString m_path;
    std::unique_ptr<AVFormatContext, FormatContextDeleter> m_formatContext;
    std::unique_ptr<AVCodecContext, CodecContextDeleter> m_codecContext;
    std::unique_ptr<AVPacket, PacketDeleter> m_packet;
    std::unique_ptr<AVFrame, FrameDeleter> m_frame;
    AVStream* m_stream = nullptr;
    int m_streamIndex = -1;
    bool m_draining = false;
};

bool VideoFrameReader::Impl::open(const QString& path, QString* errorMessage) {
    close();
    m_path = path;

    const QByteArray encodedPath = QFile::encodeName(path);
    AVFormatContext* rawFormatContext = nullptr;
    int result = avformat_open_input(&rawFormatContext, encodedPath.constData(), nullptr, nullptr);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open video: %1").arg(avErrorToString(result));
        }
        return false;
    }
    m_formatContext.reset(rawFormatContext);

    result = avformat_find_stream_info(m_formatContext.get(), nullptr);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read stream info: %1").arg(avErrorToString(result));
        }
        close();
        return false;
    }

    m_streamIndex = av_find_best_stream(m_formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_streamIndex < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No video stream found");
        }
        close();
        return false;
    }

    m_stream = m_formatContext->streams[m_streamIndex];
    const AVCodec* codec = avcodec_find_decoder(m_stream->codecpar->codec_id);
    if (!codec) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No decoder found for video stream");
        }
        close();
        return false;
    }

    m_codecContext.reset(avcodec_alloc_context3(codec));
    if (!m_codecContext) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not allocate codec context");
        }
        close();
        return false;
    }

    result = avcodec_parameters_to_context(m_codecContext.get(), m_stream->codecpar);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not copy codec parameters: %1").arg(avErrorToString(result));
        }
        close();
        return false;
    }

    result = avcodec_open2(m_codecContext.get(), codec, nullptr);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open decoder: %1").arg(avErrorToString(result));
        }
        close();
        return false;
    }

    m_packet.reset(av_packet_alloc());
    m_frame.reset(av_frame_alloc());
    if (!m_packet || !m_frame) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not allocate decode buffers");
        }
        close();
        return false;
    }

    m_draining = false;
    return true;
}

std::optional<DecodedVideoFrame> VideoFrameReader::Impl::readNextFrame(QString* errorMessage) {
    if (!isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Video reader is not open");
        }
        return std::nullopt;
    }

    while (true) {
        if (auto decoded = receiveFrame(errorMessage)) {
            return decoded;
        }

        if (m_draining) {
            return std::nullopt;
        }

        const int readResult = av_read_frame(m_formatContext.get(), m_packet.get());
        if (readResult < 0) {
            avcodec_send_packet(m_codecContext.get(), nullptr);
            m_draining = true;
            continue;
        }

        if (m_packet->stream_index != m_streamIndex) {
            av_packet_unref(m_packet.get());
            continue;
        }

        const int sendResult = avcodec_send_packet(m_codecContext.get(), m_packet.get());
        av_packet_unref(m_packet.get());
        if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
            qCWarning(logVideo) << "Could not send video packet:" << avErrorToString(sendResult);
        }
    }
}

void VideoFrameReader::Impl::close() {
    m_frame.reset();
    m_packet.reset();
    m_codecContext.reset();
    m_formatContext.reset();
    m_stream = nullptr;
    m_streamIndex = -1;
    m_draining = false;
}

bool VideoFrameReader::Impl::isOpen() const {
    return m_formatContext && m_codecContext && m_stream && m_streamIndex >= 0;
}

std::optional<DecodedVideoFrame> VideoFrameReader::Impl::receiveFrame(QString* errorMessage) {
    const int receiveResult = avcodec_receive_frame(m_codecContext.get(), m_frame.get());
    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
        return std::nullopt;
    }
    if (receiveResult < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not receive frame: %1").arg(avErrorToString(receiveResult));
        }
        return std::nullopt;
    }

    DecodedVideoFrame decoded{
        frameToImage(m_frame.get(), m_codecContext.get()),
        frameTimestampMs(m_frame.get(), m_stream)
    };
    av_frame_unref(m_frame.get());

    if (decoded.image.isNull()) {
        return std::nullopt;
    }
    return decoded;
}

#else

class VideoFrameReader::Impl {
public:
    bool open(const QString&, QString* errorMessage) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("FFmpeg libraries were not found at build time");
        }
        qCInfo(logVideo) << "Skipping video decode; FFmpeg libraries are not linked";
        return false;
    }

    std::optional<DecodedVideoFrame> readNextFrame(QString*) {
        return std::nullopt;
    }

    void close() {
    }

    bool isOpen() const {
        return false;
    }
};

#endif

VideoFrameReader::VideoFrameReader()
    : m_impl(std::make_unique<Impl>()) {
}

VideoFrameReader::~VideoFrameReader() = default;

bool VideoFrameReader::open(const QString& path, QString* errorMessage) {
    return m_impl->open(path, errorMessage);
}

std::optional<DecodedVideoFrame> VideoFrameReader::readNextFrame(QString* errorMessage) {
    return m_impl->readNextFrame(errorMessage);
}

void VideoFrameReader::close() {
    m_impl->close();
}

bool VideoFrameReader::isOpen() const {
    return m_impl->isOpen();
}

QImage extractFirstVideoFrame(const QString& path, QString* errorMessage) {
    VideoFrameReader reader;
    if (!reader.open(path, errorMessage)) {
        return {};
    }

    std::optional<DecodedVideoFrame> frame = reader.readNextFrame(errorMessage);
    if (!frame) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("No decodable video frame found");
        }
        return {};
    }

    qCInfo(logVideo) << "Extracted first video frame" << frame->image.size() << "from" << path;
    return frame->image;
}
