#include "video_frame_extractor.hpp"

#include "../util/performance_log.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

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
constexpr int kMaximumVideoDimension = 8192;
constexpr qint64 kMaximumVideoPixels = 34LL * 1024LL * 1024LL;

/** @brief Lazily resolves the FFmpeg API without adding DLLs to the executable import table. */
class FfmpegApi final {
public:
    /** @brief Loads the FFmpeg libraries and resolves every required symbol once. */
    bool ensure_loaded(QString* error_message) {
        QMutexLocker lock(&mutex_);
        if (load_attempted_) {
            if (!available_ && error_message) {
                *error_message = load_error_;
            }
            return available_;
        }

        load_attempted_ = true;
        QElapsedTimer timer;
        timer.start();

        configure_library_names();
        available_ = load_library(avutil_) &&
                     load_library(swscale_) &&
                     load_library(avcodec_) &&
                     load_library(avformat_) &&
                     resolve_symbols();

        if (!available_) {
            unload_libraries();
            if (error_message) {
                *error_message = load_error_;
            }
        }

        performance_log::record_duration(
            QStringLiteral("media.ffmpeg_runtime_load"),
            timer.elapsed(),
            {{QStringLiteral("outcome"), available_ ? QStringLiteral("loaded") : QStringLiteral("failed")}});
        return available_;
    }

    decltype(&::avformat_open_input) avformat_open_input_fn = nullptr;
    decltype(&::avformat_close_input) avformat_close_input_fn = nullptr;
    decltype(&::avformat_find_stream_info) avformat_find_stream_info_fn = nullptr;
    decltype(&::av_find_best_stream) av_find_best_stream_fn = nullptr;
    decltype(&::av_read_frame) av_read_frame_fn = nullptr;

    decltype(&::avcodec_find_decoder) avcodec_find_decoder_fn = nullptr;
    decltype(&::avcodec_alloc_context3) avcodec_alloc_context3_fn = nullptr;
    decltype(&::avcodec_free_context) avcodec_free_context_fn = nullptr;
    decltype(&::avcodec_parameters_to_context) avcodec_parameters_to_context_fn = nullptr;
    decltype(&::avcodec_open2) avcodec_open2_fn = nullptr;
    decltype(&::avcodec_receive_frame) avcodec_receive_frame_fn = nullptr;
    decltype(&::avcodec_send_packet) avcodec_send_packet_fn = nullptr;
    decltype(&::av_packet_alloc) av_packet_alloc_fn = nullptr;
    decltype(&::av_packet_free) av_packet_free_fn = nullptr;
    decltype(&::av_packet_unref) av_packet_unref_fn = nullptr;

    decltype(&::av_frame_alloc) av_frame_alloc_fn = nullptr;
    decltype(&::av_frame_free) av_frame_free_fn = nullptr;
    decltype(&::av_frame_unref) av_frame_unref_fn = nullptr;
    decltype(&::av_strerror) av_strerror_fn = nullptr;

    decltype(&::sws_getContext) sws_get_context_fn = nullptr;
    decltype(&::sws_scale) sws_scale_fn = nullptr;
    decltype(&::sws_freeContext) sws_free_context_fn = nullptr;

private:
    /** @brief Configures platform-specific FFmpeg library names. */
    void configure_library_names() {
#if defined(Q_OS_WIN)
        const QDir application_directory(QCoreApplication::applicationDirPath());
        set_windows_library_name(
            avutil_, application_directory,
            QStringLiteral("avutil-%1.dll").arg(LIBAVUTIL_VERSION_MAJOR));
        set_windows_library_name(
            swscale_, application_directory,
            QStringLiteral("swscale-%1.dll").arg(LIBSWSCALE_VERSION_MAJOR));
        set_windows_library_name(
            avcodec_, application_directory,
            QStringLiteral("avcodec-%1.dll").arg(LIBAVCODEC_VERSION_MAJOR));
        set_windows_library_name(
            avformat_, application_directory,
            QStringLiteral("avformat-%1.dll").arg(LIBAVFORMAT_VERSION_MAJOR));
#else
        avutil_.setFileName(QStringLiteral("avutil"));
        swscale_.setFileName(QStringLiteral("swscale"));
        avcodec_.setFileName(QStringLiteral("avcodec"));
        avformat_.setFileName(QStringLiteral("avformat"));
#endif
    }

#if defined(Q_OS_WIN)
    /** @brief Prefers an app-local DLL while retaining a development-build fallback. */
    static void set_windows_library_name(
        QLibrary& library,
        const QDir& application_directory,
        const QString& file_name) {
        const QString local_path = application_directory.filePath(file_name);
        library.setFileName(QFileInfo::exists(local_path) ? local_path : file_name);
    }
#endif

    /** @brief Loads one runtime library and records a useful error on failure. */
    bool load_library(QLibrary& library) {
        if (library.load()) {
            return true;
        }
        load_error_ = QStringLiteral("Could not load %1: %2")
                          .arg(library.fileName(), library.errorString());
        return false;
    }

    /** @brief Resolves a single symbol from an already-loaded library. */
    template <typename Function>
    bool resolve_symbol(QLibrary& library, const char* name, Function* function) {
        *function = reinterpret_cast<Function>(library.resolve(name));
        if (*function) {
            return true;
        }
        load_error_ = QStringLiteral("Could not resolve FFmpeg symbol %1 from %2: %3")
                          .arg(QString::fromLatin1(name), library.fileName(), library.errorString());
        return false;
    }

    /** @brief Resolves all FFmpeg entry points used by the video reader. */
    bool resolve_symbols() {
#define UIL_RESOLVE(library, symbol) \
    if (!resolve_symbol(library, #symbol, &symbol##_fn)) { \
        return false; \
    }

        UIL_RESOLVE(avformat_, avformat_open_input)
        UIL_RESOLVE(avformat_, avformat_close_input)
        UIL_RESOLVE(avformat_, avformat_find_stream_info)
        UIL_RESOLVE(avformat_, av_find_best_stream)
        UIL_RESOLVE(avformat_, av_read_frame)

        UIL_RESOLVE(avcodec_, avcodec_find_decoder)
        UIL_RESOLVE(avcodec_, avcodec_alloc_context3)
        UIL_RESOLVE(avcodec_, avcodec_free_context)
        UIL_RESOLVE(avcodec_, avcodec_parameters_to_context)
        UIL_RESOLVE(avcodec_, avcodec_open2)
        UIL_RESOLVE(avcodec_, avcodec_receive_frame)
        UIL_RESOLVE(avcodec_, avcodec_send_packet)
        UIL_RESOLVE(avcodec_, av_packet_alloc)
        UIL_RESOLVE(avcodec_, av_packet_free)
        UIL_RESOLVE(avcodec_, av_packet_unref)

        UIL_RESOLVE(avutil_, av_frame_alloc)
        UIL_RESOLVE(avutil_, av_frame_free)
        UIL_RESOLVE(avutil_, av_frame_unref)
        UIL_RESOLVE(avutil_, av_strerror)

        if (!resolve_symbol(swscale_, "sws_getContext", &sws_get_context_fn) ||
            !resolve_symbol(swscale_, "sws_scale", &sws_scale_fn) ||
            !resolve_symbol(swscale_, "sws_freeContext", &sws_free_context_fn)) {
            return false;
        }

#undef UIL_RESOLVE
        return true;
    }

    /** @brief Unloads libraries after a partial or failed initialization. */
    void unload_libraries() {
        avformat_.unload();
        avcodec_.unload();
        swscale_.unload();
        avutil_.unload();
    }

    QLibrary avutil_;
    QLibrary swscale_;
    QLibrary avcodec_;
    QLibrary avformat_;
    QMutex mutex_;
    QString load_error_;
    bool load_attempted_ = false;
    bool available_ = false;
};

/** @brief Returns the process-lifetime lazily loaded FFmpeg API. */
FfmpegApi& ffmpeg_api() {
    static FfmpegApi* api = new FfmpegApi();
    return *api;
}

struct FormatContextDeleter {
    /** @brief Closes and releases an FFmpeg format context. */
    void operator()(AVFormatContext* context) const {
        if (context) {
            ffmpeg_api().avformat_close_input_fn(&context);
        }
    }
};

struct CodecContextDeleter {
    /** @brief Releases an FFmpeg codec context. */
    void operator()(AVCodecContext* context) const {
        ffmpeg_api().avcodec_free_context_fn(&context);
    }
};

struct FrameDeleter {
    /** @brief Releases an FFmpeg frame. */
    void operator()(AVFrame* frame) const {
        ffmpeg_api().av_frame_free_fn(&frame);
    }
};

struct PacketDeleter {
    /** @brief Releases an FFmpeg packet. */
    void operator()(AVPacket* packet) const {
        ffmpeg_api().av_packet_free_fn(&packet);
    }
};

struct SwsContextDeleter {
    /** @brief Releases an FFmpeg image-scaling context. */
    void operator()(SwsContext* context) const {
        ffmpeg_api().sws_free_context_fn(context);
    }
};

/** @brief Converts an FFmpeg error code to a readable string. */
QString av_error_to_string(int error_code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    ffmpeg_api().av_strerror_fn(error_code, buffer.data(), buffer.size());
    return QString::fromLocal8Bit(buffer.data());
}

/** @brief Returns whether decoded frame dimensions fit the application's memory budget. */
bool is_safe_frame_size(int width, int height) {
    return width > 0
        && height > 0
        && width <= kMaximumVideoDimension
        && height <= kMaximumVideoDimension
        && qint64(width) * qint64(height) <= kMaximumVideoPixels;
}

/** @brief Converts a decoded FFmpeg frame to a detached Qt image. */
QImage frame_to_image(const AVFrame* frame, AVCodecContext* codec_context) {
    if (!frame || !codec_context || !is_safe_frame_size(frame->width, frame->height)) {
        return {};
    }
    std::unique_ptr<SwsContext, SwsContextDeleter> swsContext(
        ffmpeg_api().sws_get_context_fn(
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
    const int convertedHeight = ffmpeg_api().sws_scale_fn(
        swsContext.get(),
        frame->data,
        frame->linesize,
        0,
        frame->height,
        destinationData,
        destinationLinesize);

    if (convertedHeight <= 0) {
        return {};
    }

    return image;
}

/** @brief Converts a decoded frame timestamp to milliseconds. */
qint64 frame_timestamp_ms(const AVFrame* frame, const AVStream* stream) {
    const int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) {
        return 0;
    }
    return qint64(double(timestamp) * av_q2d(stream->time_base) * 1000.0);
}
}

class VideoFrameReader::Impl {
public:
    /** @brief Opens a video input and initializes its decoder. */
    bool open(const QString& path, QString* error_message);
    /** @brief Decodes and returns the next video frame. */
    std::optional<DecodedVideoFrame> read_next_frame(QString* error_message);
    /** @brief Releases the current video input and decoder state. */
    void close();
    /** @brief Returns whether the decoder input is open. */
    bool is_open() const;

private:
    /** @brief Receives one available frame from the active decoder. */
    std::optional<DecodedVideoFrame> receive_frame(QString* error_message);

    QString path_;
    std::unique_ptr<AVFormatContext, FormatContextDeleter> format_context_;
    std::unique_ptr<AVCodecContext, CodecContextDeleter> codec_context_;
    std::unique_ptr<AVPacket, PacketDeleter> packet_;
    std::unique_ptr<AVFrame, FrameDeleter> frame_;
    AVStream* stream_ = nullptr;
    int stream_index_ = -1;
    bool draining_ = false;
};

bool VideoFrameReader::Impl::open(const QString& path, QString* error_message) {
    close();
    path_ = path;

    QString load_error;
    if (!ffmpeg_api().ensure_loaded(&load_error)) {
        if (error_message) {
            *error_message = load_error;
        }
        qCWarning(logVideo) << load_error;
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(path);
    AVFormatContext* rawFormatContext = nullptr;
    int result = ffmpeg_api().avformat_open_input_fn(
        &rawFormatContext, encodedPath.constData(), nullptr, nullptr);
    if (result < 0) {
        if (error_message) {
            *error_message = QStringLiteral("Could not open video: %1").arg(av_error_to_string(result));
        }
        return false;
    }
    format_context_.reset(rawFormatContext);

    result = ffmpeg_api().avformat_find_stream_info_fn(format_context_.get(), nullptr);
    if (result < 0) {
        if (error_message) {
            *error_message = QStringLiteral("Could not read stream info: %1").arg(av_error_to_string(result));
        }
        close();
        return false;
    }

    stream_index_ = ffmpeg_api().av_find_best_stream_fn(
        format_context_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
        if (error_message) {
            *error_message = QStringLiteral("No video stream found");
        }
        close();
        return false;
    }

    stream_ = format_context_->streams[stream_index_];
    const AVCodec* codec = ffmpeg_api().avcodec_find_decoder_fn(stream_->codecpar->codec_id);
    if (!codec) {
        if (error_message) {
            *error_message = QStringLiteral("No decoder found for video stream");
        }
        close();
        return false;
    }

    codec_context_.reset(ffmpeg_api().avcodec_alloc_context3_fn(codec));
    if (!codec_context_) {
        if (error_message) {
            *error_message = QStringLiteral("Could not allocate codec context");
        }
        close();
        return false;
    }

    result = ffmpeg_api().avcodec_parameters_to_context_fn(codec_context_.get(), stream_->codecpar);
    if (result < 0) {
        if (error_message) {
            *error_message = QStringLiteral("Could not copy codec parameters: %1").arg(av_error_to_string(result));
        }
        close();
        return false;
    }

    if (codec_context_->width > 0
        && codec_context_->height > 0
        && !is_safe_frame_size(codec_context_->width, codec_context_->height)) {
        if (error_message) {
            *error_message = QStringLiteral("Video dimensions exceed the safe decoding limit");
        }
        close();
        return false;
    }

    result = ffmpeg_api().avcodec_open2_fn(codec_context_.get(), codec, nullptr);
    if (result < 0) {
        if (error_message) {
            *error_message = QStringLiteral("Could not open decoder: %1").arg(av_error_to_string(result));
        }
        close();
        return false;
    }

    if (!is_safe_frame_size(codec_context_->width, codec_context_->height)) {
        if (error_message) {
            *error_message = QStringLiteral("Video dimensions exceed the safe decoding limit");
        }
        close();
        return false;
    }

    packet_.reset(ffmpeg_api().av_packet_alloc_fn());
    frame_.reset(ffmpeg_api().av_frame_alloc_fn());
    if (!packet_ || !frame_) {
        if (error_message) {
            *error_message = QStringLiteral("Could not allocate decode buffers");
        }
        close();
        return false;
    }

    draining_ = false;
    return true;
}

std::optional<DecodedVideoFrame> VideoFrameReader::Impl::read_next_frame(QString* error_message) {
    if (error_message) {
        error_message->clear();
    }
    if (!is_open()) {
        if (error_message) {
            *error_message = QStringLiteral("Video reader is not open");
        }
        return std::nullopt;
    }

    while (true) {
        if (auto decoded = receive_frame(error_message)) {
            return decoded;
        }
        if (error_message && !error_message->isEmpty()) {
            return std::nullopt;
        }

        if (draining_) {
            return std::nullopt;
        }

        const int readResult = ffmpeg_api().av_read_frame_fn(format_context_.get(), packet_.get());
        if (readResult < 0) {
            ffmpeg_api().avcodec_send_packet_fn(codec_context_.get(), nullptr);
            draining_ = true;
            continue;
        }

        if (packet_->stream_index != stream_index_) {
            ffmpeg_api().av_packet_unref_fn(packet_.get());
            continue;
        }

        const int sendResult = ffmpeg_api().avcodec_send_packet_fn(codec_context_.get(), packet_.get());
        ffmpeg_api().av_packet_unref_fn(packet_.get());
        if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
            qCWarning(logVideo) << "Could not send video packet:" << av_error_to_string(sendResult);
        }
    }
}

void VideoFrameReader::Impl::close() {
    frame_.reset();
    packet_.reset();
    codec_context_.reset();
    format_context_.reset();
    stream_ = nullptr;
    stream_index_ = -1;
    draining_ = false;
}

bool VideoFrameReader::Impl::is_open() const {
    return format_context_ && codec_context_ && stream_ && stream_index_ >= 0;
}

std::optional<DecodedVideoFrame> VideoFrameReader::Impl::receive_frame(QString* error_message) {
    const int receiveResult = ffmpeg_api().avcodec_receive_frame_fn(codec_context_.get(), frame_.get());
    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
        return std::nullopt;
    }
    if (receiveResult < 0) {
        if (error_message) {
            *error_message = QStringLiteral("Could not receive frame: %1").arg(av_error_to_string(receiveResult));
        }
        return std::nullopt;
    }

    if (!is_safe_frame_size(frame_->width, frame_->height)) {
        if (error_message) {
            *error_message = QStringLiteral("Decoded frame dimensions exceed the safe limit");
        }
        ffmpeg_api().av_frame_unref_fn(frame_.get());
        return std::nullopt;
    }

    DecodedVideoFrame decoded{
        frame_to_image(frame_.get(), codec_context_.get()),
        frame_timestamp_ms(frame_.get(), stream_)
    };
    ffmpeg_api().av_frame_unref_fn(frame_.get());

    if (decoded.image.isNull()) {
        return std::nullopt;
    }
    return decoded;
}

#else

class VideoFrameReader::Impl {
public:
    /** @brief Reports that video decoding is unavailable in this build. */
    bool open(const QString&, QString* error_message) {
        if (error_message) {
            *error_message = QStringLiteral("FFmpeg libraries were not found at build time");
        }
        qCInfo(logVideo) << "Skipping video decode; FFmpeg libraries are not linked";
        return false;
    }

    /** @brief Returns no frame because video decoding is unavailable. */
    std::optional<DecodedVideoFrame> read_next_frame(QString*) {
        return std::nullopt;
    }

    /** @brief Performs no work because no decoder can be opened. */
    void close() {
    }

    /** @brief Returns false because video decoding is unavailable. */
    bool is_open() const {
        return false;
    }
};

#endif

VideoFrameReader::VideoFrameReader()
    : impl_(std::make_unique<Impl>()) {
}

VideoFrameReader::~VideoFrameReader() = default;

bool VideoFrameReader::open(const QString& path, QString* error_message) {
    return impl_->open(path, error_message);
}

std::optional<DecodedVideoFrame> VideoFrameReader::read_next_frame(QString* error_message) {
    return impl_->read_next_frame(error_message);
}

void VideoFrameReader::close() {
    impl_->close();
}

bool VideoFrameReader::is_open() const {
    return impl_->is_open();
}

QImage extract_first_video_frame(const QString& path, QString* error_message) {
    VideoFrameReader reader;
    if (!reader.open(path, error_message)) {
        return {};
    }

    std::optional<DecodedVideoFrame> frame = reader.read_next_frame(error_message);
    if (!frame) {
        if (error_message && error_message->isEmpty()) {
            *error_message = QStringLiteral("No decodable video frame found");
        }
        return {};
    }

    qCInfo(logVideo) << "Extracted first video frame" << frame->image.size() << "from" << path;
    return frame->image;
}
