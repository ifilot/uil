#include "VideoFrameExtractor.hpp"

#include <QByteArray>
#include <QFile>
#include <QLoggingCategory>

#include <array>
#include <memory>

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
}
#endif

QImage extractFirstVideoFrame(const QString& path, QString* errorMessage) {
#if !defined(UIL_HAVE_FFMPEG)
    if (errorMessage) {
        *errorMessage = QStringLiteral("FFmpeg libraries were not found at build time");
    }
    qCInfo(logVideo) << "Skipping first-frame extraction; FFmpeg libraries are not linked";
    return {};
#else
    const QByteArray encodedPath = QFile::encodeName(path);

    AVFormatContext* rawFormatContext = nullptr;
    int result = avformat_open_input(&rawFormatContext, encodedPath.constData(), nullptr, nullptr);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open video: %1").arg(avErrorToString(result));
        }
        return {};
    }
    std::unique_ptr<AVFormatContext, FormatContextDeleter> formatContext(rawFormatContext);

    result = avformat_find_stream_info(formatContext.get(), nullptr);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read stream info: %1").arg(avErrorToString(result));
        }
        return {};
    }

    const int streamIndex = av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No video stream found");
        }
        return {};
    }

    AVStream* stream = formatContext->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No decoder found for video stream");
        }
        return {};
    }

    std::unique_ptr<AVCodecContext, CodecContextDeleter> codecContext(avcodec_alloc_context3(codec));
    if (!codecContext) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not allocate codec context");
        }
        return {};
    }

    result = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not copy codec parameters: %1").arg(avErrorToString(result));
        }
        return {};
    }

    result = avcodec_open2(codecContext.get(), codec, nullptr);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open decoder: %1").arg(avErrorToString(result));
        }
        return {};
    }

    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (!packet || !frame) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not allocate decode buffers");
        }
        return {};
    }

    while (av_read_frame(formatContext.get(), packet.get()) >= 0) {
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }

        result = avcodec_send_packet(codecContext.get(), packet.get());
        av_packet_unref(packet.get());
        if (result < 0) {
            continue;
        }

        while ((result = avcodec_receive_frame(codecContext.get(), frame.get())) >= 0) {
            QImage image = frameToImage(frame.get(), codecContext.get());
            if (!image.isNull()) {
                qCInfo(logVideo) << "Extracted first video frame" << image.size() << "from" << path;
                return image;
            }
            av_frame_unref(frame.get());
        }

        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            break;
        }
    }

    avcodec_send_packet(codecContext.get(), nullptr);
    while (avcodec_receive_frame(codecContext.get(), frame.get()) >= 0) {
        QImage image = frameToImage(frame.get(), codecContext.get());
        if (!image.isNull()) {
            qCInfo(logVideo) << "Extracted first flushed video frame" << image.size() << "from" << path;
            return image;
        }
        av_frame_unref(frame.get());
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("No decodable video frame found");
    }
    return {};
#endif
}
