#include "media/video_frame_buffer.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>
#include <utility>

namespace {
/** Width that is not a multiple of 8, which exercises the converters' row-tail handling. */
constexpr int kVideoWidth = 322;
constexpr int kVideoHeight = 182;

/** @brief Limited-range BT.601 YCbCr triple used to synthesize test video frames. */
struct YuvColor {
    quint8 y;
    quint8 u;
    quint8 v;
};

constexpr YuvColor kYuvRed{81, 90, 240};
constexpr YuvColor kYuvGreen{145, 54, 34};
constexpr YuvColor kYuvBlue{41, 240, 110};

/**
 * @brief Writes an uncompressed 25 fps YUV4MPEG2 (4:2:0) video with one solid colour per frame.
 *
 * @return True when the file was written completely.
 */
bool write_y4m_video(const QString& path, const QList<YuvColor>& frames) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray header = QStringLiteral("YUV4MPEG2 W%1 H%2 F25:1 Ip A1:1 C420jpeg\n")
                                  .arg(kVideoWidth)
                                  .arg(kVideoHeight)
                                  .toLatin1();
    if (file.write(header) != header.size()) {
        return false;
    }
    const qsizetype luma_size = qsizetype(kVideoWidth) * kVideoHeight;
    const qsizetype chroma_size = qsizetype(kVideoWidth / 2) * (kVideoHeight / 2);
    for (const YuvColor& color : frames) {
        QByteArray frame("FRAME\n");
        frame.append(luma_size, char(color.y));
        frame.append(chroma_size, char(color.u));
        frame.append(chroma_size, char(color.v));
        if (file.write(frame) != frame.size()) {
            return false;
        }
    }
    return true;
}

/** @brief Returns whether every pixel's raw alpha byte is 0xFF (QImage::pixel() would hide it). */
bool has_opaque_alpha_bytes(const QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const quint32*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if ((line[x] >> 24) != 0xFF) {
                return false;
            }
        }
    }
    return true;
}

/** @brief Returns whether @p rgb is dominated by the channel selected in @p expected. */
bool is_dominant_colour(QRgb rgb, Qt::GlobalColor expected) {
    const int red = qRed(rgb);
    const int green = qGreen(rgb);
    const int blue = qBlue(rgb);
    switch (expected) {
    case Qt::red:
        return red > 200 && green < 60 && blue < 60;
    case Qt::green:
        return green > 200 && red < 60 && blue < 60;
    case Qt::blue:
        return blue > 200 && red < 60 && green < 60;
    default:
        return false;
    }
}

/** @brief Returns whether @p error means the FFmpeg runtime itself is unavailable. */
bool is_missing_ffmpeg_runtime(const QString& error) {
    return error.startsWith(QStringLiteral("Could not load"))
        || error.startsWith(QStringLiteral("Could not resolve"))
        || error.startsWith(QStringLiteral("FFmpeg libraries were not found"));
}

/** @brief Builds a small opaque frame in the decoder's output format at @p pts_ms. */
DecodedVideoFrame frame_at(qint64 pts_ms, const QColor& color = Qt::red) {
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(color);
    return {image, pts_ms};
}

/** @brief Scripted frame source that replays a fixed list of frames. */
class FakeFrameSource final : public VideoFrameSource {
public:
    /** @brief Creates a source that fails with @p open_error or ends with @p final_error. */
    FakeFrameSource(
        QList<DecodedVideoFrame> frames,
        QString open_error = {},
        QString final_error = {})
        : frames_(std::move(frames)),
          open_error_(std::move(open_error)),
          final_error_(std::move(final_error)) {
    }

    /** @brief Records @p path and fails when an open error was scripted. */
    bool open(const QString& path, QString* error_message) override {
        opened_path_ = path;
        if (open_error_.isEmpty()) {
            return true;
        }
        if (error_message) {
            *error_message = open_error_;
        }
        return false;
    }

    /** @brief Returns the next scripted frame, then the scripted end-of-stream outcome. */
    std::optional<DecodedVideoFrame> read_next_frame(
        QString* error_message) override {
        if (!frames_.isEmpty()) {
            return frames_.takeFirst();
        }
        if (error_message) {
            *error_message = final_error_;
        }
        return std::nullopt;
    }

private:
    QList<DecodedVideoFrame> frames_;
    QString open_error_;
    QString final_error_;
    QString opened_path_;
};

/** @brief Source whose reads block until the buffer raises its interrupt flag. */
class BlockingFrameSource final : public VideoFrameSource {
public:
    /** @brief Creates a source that reports through @p read_started once a read blocks. */
    explicit BlockingFrameSource(std::shared_ptr<std::atomic<bool>> read_started)
        : read_started_(std::move(read_started)) {
    }

    /** @brief Stores the interrupt flag raised by VideoFrameBuffer::stop(). */
    void set_interrupt_flag(const std::atomic<bool>* interrupt_requested) override {
        interrupt_requested_ = interrupt_requested;
    }

    /** @brief Always opens successfully. */
    bool open(const QString&, QString*) override {
        return true;
    }

    /** @brief Blocks until interrupted (or a 10 s safety timeout), then reports an error. */
    std::optional<DecodedVideoFrame> read_next_frame(
        QString* error_message) override {
        read_started_->store(true);
        QElapsedTimer timer;
        timer.start();
        while (!(interrupt_requested_ && interrupt_requested_->load())
               && timer.elapsed() < 10'000) {
            QThread::msleep(1);
        }
        if (error_message) {
            *error_message = QStringLiteral("interrupted");
        }
        return std::nullopt;
    }

private:
    std::shared_ptr<std::atomic<bool>> read_started_;
    const std::atomic<bool>* interrupt_requested_ = nullptr;
};
}  // namespace

/** @brief Unit tests for VideoFrameBuffer using scripted frame sources. */
class VideoFrameBufferTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies frames are queued and taken in decode order with duration tracking. */
    void buffers_frames_in_timestamp_order();
    /** @brief Verifies open and mid-stream failures are reported through failed(). */
    void reports_open_and_decode_failures();
    /** @brief Verifies stop() empties the queue and marks the buffer finished. */
    void stop_clears_buffered_state();
    /** @brief Verifies every start() creates a new source from the factory. */
    void restart_uses_a_fresh_source();
    /** @brief Verifies a never-started buffer reports that no frames will arrive. */
    void unstarted_buffer_is_finished();
    /** @brief Verifies stop() raises the interrupt flag so a blocked source returns promptly. */
    void stop_interrupts_a_blocked_source();
    /** @brief Verifies notifications queued by a stopped run are never delivered. */
    void stop_discards_pending_notifications();
    /** @brief Verifies the FFmpeg reader yields opaque RGB32 frames with correct colours. */
    void reader_decodes_opaque_rgb32_frames();
    /** @brief Verifies first-frame extraction returns the first frame of a real video. */
    void extract_first_video_frame_returns_first_frame();
    /** @brief Verifies reading an unopened reader is reported as a failure, not end of stream. */
    void reader_reports_error_when_not_open();
    /** @brief Verifies a raised interrupt flag makes the reader fail fast. */
    void reader_interrupt_flag_aborts_decoding();
    /** @brief Verifies the default buffer decodes a real video to the end. */
    void buffer_decodes_real_video_to_the_end();
};

void VideoFrameBufferTest::buffers_frames_in_timestamp_order() {
    VideoFrameBuffer buffer([] {
        return std::make_unique<FakeFrameSource>(QList<DecodedVideoFrame>{
            frame_at(0), frame_at(40), frame_at(80)});
    });
    QSignalSpy finished_spy(&buffer, &VideoFrameBuffer::finished);

    buffer.start(QStringLiteral("synthetic.mp4"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(finished_spy.size(), 1, 1000);
    QVERIFY(buffer.has_frames());
    QCOMPARE(buffer.buffered_duration_ms(), 113);

    QCOMPARE(buffer.take_frame()->pts_ms, 0);
    QCOMPARE(buffer.buffered_duration_ms(), 73);
    QCOMPARE(buffer.take_frame()->pts_ms, 40);
    QCOMPARE(buffer.take_frame()->pts_ms, 80);
    QVERIFY(!buffer.take_frame().has_value());
    QVERIFY(buffer.is_finished());
}

void VideoFrameBufferTest::reports_open_and_decode_failures() {
    VideoFrameBuffer open_failure([] {
        return std::make_unique<FakeFrameSource>(
            QList<DecodedVideoFrame>{}, QStringLiteral("cannot open"));
    });
    QSignalSpy open_failed_spy(&open_failure, &VideoFrameBuffer::failed);
    open_failure.start(QStringLiteral("missing.mp4"));
    QTRY_COMPARE_WITH_TIMEOUT(open_failed_spy.size(), 1, 1000);
    QCOMPARE(open_failure.error_message(), QStringLiteral("cannot open"));

    VideoFrameBuffer decode_failure([] {
        return std::make_unique<FakeFrameSource>(
            QList<DecodedVideoFrame>{frame_at(0)},
            QString(),
            QStringLiteral("decode failed"));
    });
    QSignalSpy decode_failed_spy(&decode_failure, &VideoFrameBuffer::failed);
    decode_failure.start(QStringLiteral("broken.mp4"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(decode_failed_spy.size(), 1, 1000);
    QCOMPARE(decode_failure.error_message(), QStringLiteral("decode failed"));
    QVERIFY(decode_failure.has_frames());
}

void VideoFrameBufferTest::stop_clears_buffered_state() {
    VideoFrameBuffer buffer([] {
        return std::make_unique<FakeFrameSource>(QList<DecodedVideoFrame>{
            frame_at(0), frame_at(40), frame_at(80)});
    });
    buffer.start(QStringLiteral("synthetic.mp4"), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(buffer.has_frames(), 1000);

    buffer.stop();
    QVERIFY(!buffer.has_frames());
    QCOMPARE(buffer.buffered_duration_ms(), 0);
    QVERIFY(buffer.is_finished());
}

void VideoFrameBufferTest::restart_uses_a_fresh_source() {
    auto invocation = std::make_shared<int>(0);
    VideoFrameBuffer buffer([invocation] {
        const qint64 pts = (*invocation)++ == 0 ? 10 : 90;
        return std::make_unique<FakeFrameSource>(
            QList<DecodedVideoFrame>{frame_at(pts)});
    });

    buffer.start(QStringLiteral("first.mp4"), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(buffer.has_frames(), 1000);
    QCOMPARE(buffer.take_frame()->pts_ms, 10);

    buffer.start(QStringLiteral("second.mp4"), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(buffer.has_frames(), 1000);
    QCOMPARE(buffer.take_frame()->pts_ms, 90);
    QCOMPARE(*invocation, 2);
}

void VideoFrameBufferTest::unstarted_buffer_is_finished() {
    VideoFrameBuffer buffer([] {
        return std::make_unique<FakeFrameSource>(QList<DecodedVideoFrame>{});
    });
    QVERIFY(buffer.is_finished());
    QVERIFY(!buffer.has_frames());
    QVERIFY(!buffer.take_frame().has_value());
}

void VideoFrameBufferTest::stop_interrupts_a_blocked_source() {
    auto read_started = std::make_shared<std::atomic<bool>>(false);
    VideoFrameBuffer buffer([read_started] {
        return std::make_unique<BlockingFrameSource>(read_started);
    });
    QSignalSpy failed_spy(&buffer, &VideoFrameBuffer::failed);

    buffer.start(QStringLiteral("blocking.mp4"), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(read_started->load(), 1000);

    QElapsedTimer timer;
    timer.start();
    buffer.stop();
    QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(buffer.is_finished());
    QVERIFY(buffer.error_message().isEmpty());

    QTest::qWait(50);
    QCOMPARE(failed_spy.size(), 0);
}

void VideoFrameBufferTest::stop_discards_pending_notifications() {
    VideoFrameBuffer buffer([] {
        return std::make_unique<FakeFrameSource>(
            QList<DecodedVideoFrame>{frame_at(0), frame_at(40)},
            QString(),
            QStringLiteral("decode failed"));
    });
    QSignalSpy frame_spy(&buffer, &VideoFrameBuffer::frame_available);
    QSignalSpy buffer_spy(&buffer, &VideoFrameBuffer::buffer_changed);
    QSignalSpy failed_spy(&buffer, &VideoFrameBuffer::failed);

    buffer.start(QStringLiteral("synthetic.mp4"), 1000);
    // Wait without processing events so every notification of this run is still queued.
    QElapsedTimer timer;
    timer.start();
    while (buffer.error_message().isEmpty() && timer.elapsed() < 1000) {
        QThread::msleep(1);
    }
    QCOMPARE(buffer.error_message(), QStringLiteral("decode failed"));

    buffer.stop();
    QTest::qWait(50);
    QCOMPARE(frame_spy.size(), 0);
    QCOMPARE(buffer_spy.size(), 0);
    QCOMPARE(failed_spy.size(), 0);
}

void VideoFrameBufferTest::reader_decodes_opaque_rgb32_frames() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("colours.y4m"));
    QVERIFY(write_y4m_video(path, {kYuvRed, kYuvGreen, kYuvBlue}));

    VideoFrameReader reader;
    QString error;
    if (!reader.open(path, &error)) {
        if (is_missing_ffmpeg_runtime(error)) {
            QSKIP(qPrintable(error));
        }
        QFAIL(qPrintable(error));
    }

    const QList<Qt::GlobalColor> expected_colours{Qt::red, Qt::green, Qt::blue};
    for (int index = 0; index < expected_colours.size(); ++index) {
        std::optional<DecodedVideoFrame> frame = reader.read_next_frame(&error);
        QVERIFY2(frame.has_value(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(frame->pts_ms, qint64(index * 40));
        QCOMPARE(frame->image.size(), QSize(kVideoWidth, kVideoHeight));
        QCOMPARE(frame->image.format(), QImage::Format_RGB32);
        QVERIFY(has_opaque_alpha_bytes(frame->image));
        const Qt::GlobalColor expected = expected_colours.at(index);
        const QImage& image = frame->image;
        QVERIFY(is_dominant_colour(image.pixel(kVideoWidth / 2, kVideoHeight / 2), expected));
        QVERIFY(is_dominant_colour(image.pixel(kVideoWidth - 1, kVideoHeight - 1), expected));
    }

    // End of stream: no frame and no error, and it stays that way.
    QVERIFY(!reader.read_next_frame(&error).has_value());
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!reader.read_next_frame(&error).has_value());
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void VideoFrameBufferTest::extract_first_video_frame_returns_first_frame() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("poster.y4m"));
    QVERIFY(write_y4m_video(path, {kYuvBlue, kYuvRed}));

    QString error;
    const QImage image = extract_first_video_frame(path, &error);
    if (image.isNull() && is_missing_ffmpeg_runtime(error)) {
        QSKIP(qPrintable(error));
    }
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.format(), QImage::Format_RGB32);
    QVERIFY(has_opaque_alpha_bytes(image));
    QVERIFY(is_dominant_colour(image.pixel(0, 0), Qt::blue));
    QVERIFY(is_dominant_colour(image.pixel(kVideoWidth - 1, 0), Qt::blue));
}

void VideoFrameBufferTest::reader_reports_error_when_not_open() {
    VideoFrameReader reader;
    QString error;
    QVERIFY(!reader.read_next_frame(&error).has_value());
    QVERIFY(!error.isEmpty());
}

void VideoFrameBufferTest::reader_interrupt_flag_aborts_decoding() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("interrupted.y4m"));
    QVERIFY(write_y4m_video(path, {kYuvRed, kYuvGreen}));

    std::atomic<bool> interrupt_requested{true};
    VideoFrameReader reader;
    reader.set_interrupt_flag(&interrupt_requested);
    QString error;
    const bool opened = reader.open(path, &error);
    if (!opened && is_missing_ffmpeg_runtime(error)) {
        QSKIP(qPrintable(error));
    }
    if (opened) {
        QVERIFY(!reader.read_next_frame(&error).has_value());
    }
    QCOMPARE(error, QStringLiteral("Video decoding was interrupted"));
    reader.set_interrupt_flag(nullptr);
}

void VideoFrameBufferTest::buffer_decodes_real_video_to_the_end() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("playback.y4m"));
    QVERIFY(write_y4m_video(path, {kYuvRed, kYuvGreen, kYuvBlue, kYuvRed}));

    VideoFrameBuffer buffer;
    QSignalSpy finished_spy(&buffer, &VideoFrameBuffer::finished);
    QSignalSpy failed_spy(&buffer, &VideoFrameBuffer::failed);
    buffer.start(path, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(finished_spy.size() + failed_spy.size() > 0, 5000);
    if (!failed_spy.isEmpty()) {
        const QString error = failed_spy.first().first().toString();
        if (is_missing_ffmpeg_runtime(error)) {
            QSKIP(qPrintable(error));
        }
        QFAIL(qPrintable(error));
    }

    QList<qint64> timestamps;
    while (std::optional<DecodedVideoFrame> frame = buffer.take_frame()) {
        QCOMPARE(frame->image.format(), QImage::Format_RGB32);
        timestamps.append(frame->pts_ms);
    }
    QCOMPARE(timestamps, (QList<qint64>{0, 40, 80, 120}));
    QVERIFY(buffer.is_finished());
    QVERIFY(buffer.error_message().isEmpty());
}

QTEST_GUILESS_MAIN(VideoFrameBufferTest)

#include "video_frame_buffer_test.moc"
