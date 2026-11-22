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
#include <cstddef>
#include <new>

Q_LOGGING_CATEGORY(log_video, "video")

#if defined(UIL_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr int kMaximumVideoDimension = 8192;
constexpr qint64 kMaximumVideoPixels = 34LL * 1024LL * 1024LL;
/** Consecutive recoverable decoder errors tolerated before decoding is reported as failed. */
constexpr int kMaximumConsecutiveDecodeErrors = 32;
/** Output pixel format: native-endian 0xAARRGGBB, the memory layout of QImage::Format_RGB32. */
constexpr AVPixelFormat kOutputPixelFormat = AV_PIX_FMT_RGB32;
/** Row stride alignment of converted frames (16 pixels); see allocate_padded_image(). */
constexpr std::size_t kRowAlignmentBytes = 64;

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
        available_ = load_library(avutil_) && load_library(swscale_) && load_library(avcodec_) &&
                     load_library(avformat_) && resolve_symbols();

        if (!available_) {
            unload_libraries();
            if (error_message) {
                *error_message = load_error_;
            }
        }

        performance_log::record_duration(
            QStringLiteral("media.ffmpeg_runtime_load"), timer.elapsed(),
            {{QStringLiteral("outcome"),
              available_ ? QStringLiteral("loaded") : QStringLiteral("failed")}});
        return available_;
    }

    decltype(&::avformat_alloc_context) avformat_alloc_context_fn = nullptr;  // Optional.
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
    decltype(&::av_pix_fmt_desc_get) av_pix_fmt_desc_get_fn = nullptr;  // Optional.

    decltype(&::sws_getContext) sws_get_context_fn = nullptr;
    decltype(&::sws_getCachedContext) sws_get_cached_context_fn = nullptr;  // Optional.
    decltype(&::sws_scale) sws_scale_fn = nullptr;
    decltype(&::sws_freeContext) sws_free_context_fn = nullptr;
    decltype(&::sws_getCoefficients) sws_get_coefficients_fn = nullptr;             // Optional.
    decltype(&::sws_setColorspaceDetails) sws_set_colorspace_details_fn = nullptr;  // Optional.

private:
    /** @brief Configures platform-specific FFmpeg library names. */
    void configure_library_names() {
#if defined(Q_OS_WIN)
        const QDir application_directory(QCoreApplication::applicationDirPath());
        set_windows_library_name(avutil_, application_directory,
                                 QStringLiteral("avutil-%1.dll").arg(LIBAVUTIL_VERSION_MAJOR));
        set_windows_library_name(swscale_, application_directory,
                                 QStringLiteral("swscale-%1.dll").arg(LIBSWSCALE_VERSION_MAJOR));
        set_windows_library_name(avcodec_, application_directory,
                                 QStringLiteral("avcodec-%1.dll").arg(LIBAVCODEC_VERSION_MAJOR));
        set_windows_library_name(avformat_, application_directory,
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
    static void set_windows_library_name(QLibrary& library, const QDir& application_directory,
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
        load_error_ =
            QStringLiteral("Could not load %1: %2").arg(library.fileName(), library.errorString());
        return false;
    }

    /** @brief Resolves a single symbol from an already-loaded library. */
    template <typename Function>
    bool resolve_symbol(QLibrary& library, const char* name, Function* function) {
        *function = reinterpret_cast<Function>(library.resolve(name));
        if (*function) {
            return true;
        }
        load_error_ =
            QStringLiteral("Could not resolve FFmpeg symbol %1 from %2: %3")
                .arg(QString::fromLatin1(name), library.fileName(), library.errorString());
        return false;
    }

    /** @brief Resolves all FFmpeg entry points used by the video reader. */
    bool resolve_symbols() {
#define UIL_RESOLVE(library, symbol)                       \
    if (!resolve_symbol(library, #symbol, &symbol##_fn)) { \
        return false;                                      \
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

        // Optional entry points: each has a slower or more conservative fallback.
        resolve_optional_symbol(avformat_, "avformat_alloc_context", &avformat_alloc_context_fn);
        resolve_optional_symbol(avutil_, "av_pix_fmt_desc_get", &av_pix_fmt_desc_get_fn);
        resolve_optional_symbol(swscale_, "sws_getCachedContext", &sws_get_cached_context_fn);
        resolve_optional_symbol(swscale_, "sws_getCoefficients", &sws_get_coefficients_fn);
        resolve_optional_symbol(swscale_, "sws_setColorspaceDetails",
                                &sws_set_colorspace_details_fn);
        return true;
    }

    /**
     * @brief Resolves a symbol whose absence only disables an optimization.
     *
     * Leaves @p function null and logs a warning instead of failing the runtime load.
     */
    template <typename Function>
    static void resolve_optional_symbol(QLibrary& library, const char* name, Function* function) {
        *function = reinterpret_cast<Function>(library.resolve(name));
        if (!*function) {
            qCWarning(log_video) << "Optional FFmpeg symbol" << name << "is unavailable in"
                                 << library.fileName() << "; using a fallback";
        }
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
    void operator()(AVFrame* frame) const { ffmpeg_api().av_frame_free_fn(&frame); }
};

struct PacketDeleter {
    /** @brief Releases an FFmpeg packet. */
    void operator()(AVPacket* packet) const { ffmpeg_api().av_packet_free_fn(&packet); }
};

struct SwsContextDeleter {
    /** @brief Releases an FFmpeg image-scaling context. */
    void operator()(SwsContext* context) const { ffmpeg_api().sws_free_context_fn(context); }
};

/** @brief Converts an FFmpeg error code to a readable string. */
QString av_error_to_string(int error_code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    ffmpeg_api().av_strerror_fn(error_code, buffer.data(), buffer.size());
    return QString::fromLocal8Bit(buffer.data());
}

/** @brief Returns whether decoded frame dimensions fit the application's memory budget. */
bool is_safe_frame_size(int width, int height) {
    return width > 0 && height > 0 && width <= kMaximumVideoDimension &&
           height <= kMaximumVideoDimension &&
           qint64(width) * qint64(height) <= kMaximumVideoPixels;
}

/**
 * @brief Returns whether frames in @p format may carry a meaningful alpha channel.
 *
 * Returns true when the pixel-format descriptor API is unavailable, so translucent sources are
 * never mislabeled as opaque QImage::Format_RGB32 frames.
 */
bool pixel_format_has_alpha(AVPixelFormat format) {
    if (!ffmpeg_api().av_pix_fmt_desc_get_fn) {
        return true;
    }
    const AVPixFmtDescriptor* descriptor = ffmpeg_api().av_pix_fmt_desc_get_fn(format);
    return descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
}

/**
 * @brief Allocates an uninitialized 32-bit image whose rows are padded to kRowAlignmentBytes.
 *
 * QImage's own allocation packs rows at width * 4 bytes. FFmpeg's SIMD YUV-to-RGB converters
 * work in blocks of 8 or more pixels and silently skip the remainder of a row when a full block
 * would overrun the destination stride, which left the right-most columns of videos whose width
 * is not a multiple of 8 uninitialized. Padding every row lets them convert the whole width.
 *
 * @return The image, or a null image when the allocation fails.
 */
QImage allocate_padded_image(int width, int height, QImage::Format format) {
    const qsizetype alignment = qsizetype(kRowAlignmentBytes);
    const qsizetype bytes_per_line = (qsizetype(width) * 4 + alignment - 1) / alignment * alignment;
    void* buffer = ::operator new(std::size_t(bytes_per_line * height),
                                  std::align_val_t(kRowAlignmentBytes), std::nothrow);
    if (!buffer) {
        return {};
    }

    QImage image(
        static_cast<uchar*>(buffer), width, height, bytes_per_line, format,
        [](void* data) { ::operator delete(data, std::align_val_t(kRowAlignmentBytes)); }, buffer);
    if (image.isNull()) {
        // QImage only takes ownership through the cleanup function when construction succeeds.
        ::operator delete(buffer, std::align_val_t(kRowAlignmentBytes));
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

/** @brief Returns the error reported when the registered interrupt flag aborts decoding. */
QString interrupted_message() { return QStringLiteral("Video decoding was interrupted"); }
}  // namespace

class VideoFrameReader::Impl {
public:
    /** @brief Opens a video input and initializes its decoder; see VideoFrameReader::open(). */
    bool open(const QString& path, QString* error_message, VideoFrameReader::Threading threading);
    /** @brief Decodes the next video frame; see VideoFrameReader::read_next_frame(). */
    std::optional<DecodedVideoFrame> read_next_frame(QString* error_message);
    /** @brief Releases the current video input, decoder, scaler and buffers. */
    void close();
    /** @brief Returns whether the decoder input is open. */
    bool is_open() const;
    /** @brief Registers the cross-thread flag that aborts demuxing and decoding. */
    void set_interrupt_flag(const std::atomic<bool>* interrupt_requested);

private:
    /** @brief Outcome of a single avcodec_receive_frame() attempt. */
    enum class ReceiveStatus { Frame, NeedsInput, EndOfStream, Recoverable, Failed };

    /**
     * @brief Opens the demuxer and decoder without cleaning up on failure.
     *
     * @param error_message Receives a readable reason when false is returned.
     * @return True when decoding can start.
     */
    bool open_input(const QString& path, VideoFrameReader::Threading threading,
                    QString& error_message);
    /**
     * @brief Feeds packets to the decoder until a frame, the end of stream, or a failure.
     *
     * @param error_message Left empty at the end of the stream; set on failure.
     */
    std::optional<DecodedVideoFrame> decode_next_frame(QString& error_message);
    /**
     * @brief Receives and converts one pending frame from the decoder.
     *
     * @param decoded Receives the frame when ReceiveStatus::Frame is returned.
     * @param error_message Receives a message when ReceiveStatus::Failed is returned.
     */
    ReceiveStatus receive_frame(DecodedVideoFrame& decoded, QString& error_message);
    /**
     * @brief Converts a decoded FFmpeg frame to a detached Qt image.
     *
     * Converts straight into the QImage's storage with a scaler cached for the lifetime of the
     * input. Returns a null image when the frame cannot be converted.
     */
    QImage frame_to_image(const AVFrame* frame);
    /**
     * @brief Configures @p scaler's YUV matrix and range from @p frame's colour tags.
     *
     * Skips the update when the cached scaler is already configured for the same tags.
     * @param[in] fresh_context True when @p scaler was created for this frame only.
     */
    void apply_color_details(SwsContext* scaler, const AVFrame* frame, bool fresh_context);
    /** @brief Returns whether the registered interrupt flag has been raised. */
    bool is_interrupt_requested() const;
    /** @brief FFmpeg I/O interrupt callback; @p opaque is the owning Impl. */
    static int interrupt_callback(void* opaque);

    QString path_;
    std::unique_ptr<AVFormatContext, FormatContextDeleter> format_context_;
    std::unique_ptr<AVCodecContext, CodecContextDeleter> codec_context_;
    std::unique_ptr<AVPacket, PacketDeleter> packet_;
    std::unique_ptr<AVFrame, FrameDeleter> frame_;
    std::unique_ptr<SwsContext, SwsContextDeleter> sws_context_;
    // Context and colour-space key last passed to apply_color_details(), so the conversion
    // tables are rebuilt only when the scaler or the stream's colour tags change.
    const SwsContext* color_configured_context_ = nullptr;
    int color_configured_key_ = -1;
    const std::atomic<bool>* interrupt_requested_ = nullptr;
    AVStream* stream_ = nullptr;
    int stream_index_ = -1;
    int consecutive_decode_errors_ = 0;
    bool draining_ = false;
};

bool VideoFrameReader::Impl::open(const QString& path, QString* error_message,
                                  VideoFrameReader::Threading threading) {
    close();
    path_ = path;

    QString open_error;
    if (!open_input(path, threading, open_error)) {
        close();
        if (error_message) {
            *error_message = open_error;
        }
        return false;
    }
    return true;
}

bool VideoFrameReader::Impl::open_input(const QString& path, VideoFrameReader::Threading threading,
                                        QString& error_message) {
    if (!ffmpeg_api().ensure_loaded(&error_message)) {
        qCWarning(log_video) << error_message;
        return false;
    }

    // Pre-allocating the context lets the interrupt callback cover the file open and every
    // later read; avformat_open_input() frees it on failure.
    AVFormatContext* raw_format_context = nullptr;
    if (ffmpeg_api().avformat_alloc_context_fn) {
        raw_format_context = ffmpeg_api().avformat_alloc_context_fn();
        if (raw_format_context) {
            raw_format_context->interrupt_callback.callback = &Impl::interrupt_callback;
            raw_format_context->interrupt_callback.opaque = this;
        }
    }

    const QByteArray encoded_path = QFile::encodeName(path);
    int result = ffmpeg_api().avformat_open_input_fn(&raw_format_context, encoded_path.constData(),
                                                     nullptr, nullptr);
    if (result < 0) {
        error_message =
            is_interrupt_requested()
                ? interrupted_message()
                : QStringLiteral("Could not open video: %1").arg(av_error_to_string(result));
        return false;
    }
    format_context_.reset(raw_format_context);

    result = ffmpeg_api().avformat_find_stream_info_fn(format_context_.get(), nullptr);
    if (result < 0) {
        error_message =
            is_interrupt_requested()
                ? interrupted_message()
                : QStringLiteral("Could not read stream info: %1").arg(av_error_to_string(result));
        return false;
    }

    stream_index_ = ffmpeg_api().av_find_best_stream_fn(format_context_.get(), AVMEDIA_TYPE_VIDEO,
                                                        -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
        error_message = QStringLiteral("No video stream found");
        return false;
    }

    stream_ = format_context_->streams[stream_index_];
    const AVCodec* codec = ffmpeg_api().avcodec_find_decoder_fn(stream_->codecpar->codec_id);
    if (!codec) {
        error_message = QStringLiteral("No decoder found for video stream");
        return false;
    }

    codec_context_.reset(ffmpeg_api().avcodec_alloc_context3_fn(codec));
    if (!codec_context_) {
        error_message = QStringLiteral("Could not allocate codec context");
        return false;
    }

    result = ffmpeg_api().avcodec_parameters_to_context_fn(codec_context_.get(), stream_->codecpar);
    if (result < 0) {
        error_message =
            QStringLiteral("Could not copy codec parameters: %1").arg(av_error_to_string(result));
        return false;
    }

    if (codec_context_->width > 0 && codec_context_->height > 0 &&
        !is_safe_frame_size(codec_context_->width, codec_context_->height)) {
        error_message = QStringLiteral("Video dimensions exceed the safe decoding limit");
        return false;
    }

    if (threading == VideoFrameReader::Threading::Playback) {
        // Let FFmpeg size the pool (0 = auto). Frame threading delays output by up to
        // thread_count - 1 frames; decode_next_frame() keeps feeding packets until a frame
        // appears and flushes the decoder at the end of the input, so no frame is lost.
        codec_context_->thread_count = 0;
        codec_context_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    } else {
        // One frame has nothing to pipeline; spawning per-thread decoder contexts only adds
        // start-up cost, so decode on the calling thread.
        codec_context_->thread_count = 1;
    }

    result = ffmpeg_api().avcodec_open2_fn(codec_context_.get(), codec, nullptr);
    if (result < 0) {
        error_message =
            QStringLiteral("Could not open decoder: %1").arg(av_error_to_string(result));
        return false;
    }

    if (!is_safe_frame_size(codec_context_->width, codec_context_->height)) {
        error_message = QStringLiteral("Video dimensions exceed the safe decoding limit");
        return false;
    }

    packet_.reset(ffmpeg_api().av_packet_alloc_fn());
    frame_.reset(ffmpeg_api().av_frame_alloc_fn());
    if (!packet_ || !frame_) {
        error_message = QStringLiteral("Could not allocate decode buffers");
        return false;
    }

    draining_ = false;
    consecutive_decode_errors_ = 0;
    return true;
}

std::optional<DecodedVideoFrame> VideoFrameReader::Impl::read_next_frame(QString* error_message) {
    // Decode against a local message so failures are detected even when the caller passes null.
    QString decode_error;
    std::optional<DecodedVideoFrame> decoded = decode_next_frame(decode_error);
    if (error_message) {
        *error_message = decode_error;
    }
    return decoded;
}

std::optional<DecodedVideoFrame> VideoFrameReader::Impl::decode_next_frame(QString& error_message) {
    if (!is_open()) {
        error_message = QStringLiteral("Video reader is not open");
        return std::nullopt;
    }

    while (true) {
        if (is_interrupt_requested()) {
            error_message = interrupted_message();
            return std::nullopt;
        }

        DecodedVideoFrame decoded;
        switch (receive_frame(decoded, error_message)) {
        case ReceiveStatus::Frame:
            return decoded;
        case ReceiveStatus::EndOfStream:
        case ReceiveStatus::Failed:
            return std::nullopt;
        case ReceiveStatus::Recoverable:
            continue;
        case ReceiveStatus::NeedsInput:
            break;
        }

        if (draining_) {
            // A flushed decoder should not ask for more input; treat it as the end of the stream.
            return std::nullopt;
        }

        const int read_result = ffmpeg_api().av_read_frame_fn(format_context_.get(), packet_.get());
        if (read_result < 0) {
            if (is_interrupt_requested()) {
                error_message = interrupted_message();
                return std::nullopt;
            }
            if (read_result != AVERROR_EOF) {
                qCWarning(log_video) << "Stopped reading video packets from" << path_ << ":"
                                     << av_error_to_string(read_result);
            }
            // Enter draining mode so buffered (frame-threaded or reordered) output is returned.
            ffmpeg_api().avcodec_send_packet_fn(codec_context_.get(), nullptr);
            draining_ = true;
            continue;
        }

        if (packet_->stream_index != stream_index_) {
            ffmpeg_api().av_packet_unref_fn(packet_.get());
            continue;
        }

        const int send_result =
            ffmpeg_api().avcodec_send_packet_fn(codec_context_.get(), packet_.get());
        ffmpeg_api().av_packet_unref_fn(packet_.get());
        if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
            qCWarning(log_video) << "Could not send video packet:"
                                 << av_error_to_string(send_result);
        }
    }
}

void VideoFrameReader::Impl::close() {
    sws_context_.reset();
    color_configured_context_ = nullptr;
    color_configured_key_ = -1;
    frame_.reset();
    packet_.reset();
    codec_context_.reset();
    format_context_.reset();
    stream_ = nullptr;
    stream_index_ = -1;
    consecutive_decode_errors_ = 0;
    draining_ = false;
}

bool VideoFrameReader::Impl::is_open() const {
    return format_context_ && codec_context_ && stream_ && stream_index_ >= 0;
}

void VideoFrameReader::Impl::set_interrupt_flag(const std::atomic<bool>* interrupt_requested) {
    interrupt_requested_ = interrupt_requested;
}

VideoFrameReader::Impl::ReceiveStatus VideoFrameReader::Impl::receive_frame(
    DecodedVideoFrame& decoded, QString& error_message) {
    const int receive_result =
        ffmpeg_api().avcodec_receive_frame_fn(codec_context_.get(), frame_.get());
    if (receive_result == AVERROR(EAGAIN)) {
        return ReceiveStatus::NeedsInput;
    }
    if (receive_result == AVERROR_EOF) {
        return ReceiveStatus::EndOfStream;
    }
    if (receive_result < 0) {
        // A corrupt packet should cost one frame, not the whole clip; give up only when the
        // decoder keeps failing.
        if (receive_result == AVERROR_INVALIDDATA &&
            ++consecutive_decode_errors_ <= kMaximumConsecutiveDecodeErrors) {
            qCWarning(log_video) << "Skipping undecodable video data:"
                                 << av_error_to_string(receive_result);
            return ReceiveStatus::Recoverable;
        }
        error_message =
            QStringLiteral("Could not receive frame: %1").arg(av_error_to_string(receive_result));
        return ReceiveStatus::Failed;
    }
    consecutive_decode_errors_ = 0;

    if (!is_safe_frame_size(frame_->width, frame_->height)) {
        error_message = QStringLiteral("Decoded frame dimensions exceed the safe limit");
        ffmpeg_api().av_frame_unref_fn(frame_.get());
        return ReceiveStatus::Failed;
    }

    decoded.image = frame_to_image(frame_.get());
    decoded.pts_ms = frame_timestamp_ms(frame_.get(), stream_);
    ffmpeg_api().av_frame_unref_fn(frame_.get());

    if (decoded.image.isNull()) {
        error_message = QStringLiteral("Could not convert decoded video frame");
        return ReceiveStatus::Failed;
    }
    return ReceiveStatus::Frame;
}

void VideoFrameReader::Impl::apply_color_details(SwsContext* scaler, const AVFrame* frame,
                                                 bool fresh_context) {
    const auto& api = ffmpeg_api();
    if (!api.sws_get_coefficients_fn || !api.sws_set_colorspace_details_fn) {
        return;
    }

    // swscale assumes BT.601 unless told otherwise. Follow the stream's tags; for untagged
    // video use the common player convention of BT.709 for HD and BT.601 for SD.
    int color_space = int(frame->colorspace);
    if (frame->colorspace == AVCOL_SPC_UNSPECIFIED || frame->colorspace == AVCOL_SPC_RESERVED ||
        frame->colorspace == AVCOL_SPC_RGB) {
        color_space = frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
    }
    const int source_full_range = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    const int key = color_space * 2 + source_full_range;
    if (!fresh_context && scaler == color_configured_context_ && key == color_configured_key_) {
        return;
    }

    // Returns -1 for RGB sources, which need no matrix; the scaler then keeps its defaults.
    api.sws_set_colorspace_details_fn(scaler, api.sws_get_coefficients_fn(color_space),
                                      source_full_range,
                                      api.sws_get_coefficients_fn(SWS_CS_DEFAULT),
                                      1,  // Full-range RGB output.
                                      0, 1 << 16, 1 << 16);
    color_configured_context_ = fresh_context ? nullptr : scaler;
    color_configured_key_ = fresh_context ? -1 : key;
}

QImage VideoFrameReader::Impl::frame_to_image(const AVFrame* frame) {
    if (!frame || !is_safe_frame_size(frame->width, frame->height)) {
        return {};
    }

    // Opaque video becomes Format_RGB32 (AV_PIX_FMT_RGB32 stores 0xAARRGGBB with A = 0xFF when
    // the source has no alpha), which Qt's raster engine draws without per-pixel blending.
    const AVPixelFormat source_format = AVPixelFormat(frame->format);
    const bool has_alpha = pixel_format_has_alpha(source_format);
    QImage image = allocate_padded_image(frame->width, frame->height,
                                         has_alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (image.isNull()) {
        return {};
    }

    std::unique_ptr<SwsContext, SwsContextDeleter> uncached_scaler;
    SwsContext* scaler = nullptr;
    if (ffmpeg_api().sws_get_cached_context_fn) {
        // Returns the same context while size and formats are unchanged; when it cannot build a
        // replacement it frees the old context and returns null.
        sws_context_.reset(ffmpeg_api().sws_get_cached_context_fn(
            sws_context_.release(), frame->width, frame->height, source_format, frame->width,
            frame->height, kOutputPixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr));
        scaler = sws_context_.get();
    } else {
        uncached_scaler.reset(ffmpeg_api().sws_get_context_fn(
            frame->width, frame->height, source_format, frame->width, frame->height,
            kOutputPixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr));
        scaler = uncached_scaler.get();
    }
    if (!scaler) {
        return {};
    }
    apply_color_details(scaler, frame, uncached_scaler != nullptr);

    // Write straight into the image, honouring its padded row stride.
    uint8_t* destination_data[4] = {image.bits(), nullptr, nullptr, nullptr};
    const int destination_linesize[4] = {int(image.bytesPerLine()), 0, 0, 0};
    const int converted_height =
        ffmpeg_api().sws_scale_fn(scaler, frame->data, frame->linesize, 0, frame->height,
                                  destination_data, destination_linesize);
    if (converted_height <= 0) {
        return {};
    }

    if (has_alpha) {
        // Premultiply once on the decoder thread instead of on every repaint.
        image.convertTo(QImage::Format_ARGB32_Premultiplied);
    }
    return image;
}

bool VideoFrameReader::Impl::is_interrupt_requested() const {
    return interrupt_requested_ && interrupt_requested_->load(std::memory_order_acquire);
}

int VideoFrameReader::Impl::interrupt_callback(void* opaque) {
    return static_cast<const Impl*>(opaque)->is_interrupt_requested() ? 1 : 0;
}

#else

class VideoFrameReader::Impl {
public:
    /** @brief Reports that video decoding is unavailable in this build. */
    bool open(const QString&, QString* error_message, VideoFrameReader::Threading) {
        if (error_message) {
            *error_message = QStringLiteral("FFmpeg libraries were not found at build time");
        }
        qCInfo(log_video) << "Skipping video decode; FFmpeg libraries are not linked";
        return false;
    }

    /** @brief Returns no frame and reports that the reader is not open. */
    std::optional<DecodedVideoFrame> read_next_frame(QString* error_message) {
        if (error_message) {
            *error_message = QStringLiteral("Video reader is not open");
        }
        return std::nullopt;
    }

    /** @brief Performs no work because no decoder can be opened. */
    void close() {}

    /** @brief Returns false because video decoding is unavailable. */
    bool is_open() const { return false; }

    /** @brief Ignores the flag because no decoding work can block. */
    void set_interrupt_flag(const std::atomic<bool>*) {}
};

#endif

VideoFrameReader::VideoFrameReader() : impl_(std::make_unique<Impl>()) {}

VideoFrameReader::~VideoFrameReader() = default;

bool VideoFrameReader::open(const QString& path, QString* error_message, Threading threading) {
    return impl_->open(path, error_message, threading);
}

std::optional<DecodedVideoFrame> VideoFrameReader::read_next_frame(QString* error_message) {
    return impl_->read_next_frame(error_message);
}

void VideoFrameReader::close() { impl_->close(); }

bool VideoFrameReader::is_open() const { return impl_->is_open(); }

void VideoFrameReader::set_interrupt_flag(const std::atomic<bool>* interrupt_requested) {
    impl_->set_interrupt_flag(interrupt_requested);
}

QImage extract_first_video_frame(const QString& path, QString* error_message) {
    VideoFrameReader reader;
    if (!reader.open(path, error_message, VideoFrameReader::Threading::SingleFrame)) {
        return {};
    }

    std::optional<DecodedVideoFrame> frame = reader.read_next_frame(error_message);
    if (!frame) {
        if (error_message && error_message->isEmpty()) {
            *error_message = QStringLiteral("No decodable video frame found");
        }
        return {};
    }

    qCInfo(log_video) << "Extracted first video frame" << frame->image.size() << "from" << path;
    return frame->image;
}
