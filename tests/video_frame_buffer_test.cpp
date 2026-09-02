#include "media/video_frame_buffer.hpp"

#include <QColor>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <utility>

namespace {
DecodedVideoFrame frame_at(qint64 pts_ms, const QColor& color = Qt::red) {
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(color);
    return {image, pts_ms};
}

class FakeFrameSource final : public VideoFrameSource {
public:
    FakeFrameSource(
        QList<DecodedVideoFrame> frames,
        QString open_error = {},
        QString final_error = {})
        : frames_(std::move(frames)),
          open_error_(std::move(open_error)),
          final_error_(std::move(final_error)) {
    }

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
}  // namespace

class VideoFrameBufferTest final : public QObject {
    Q_OBJECT

private slots:
    void buffers_frames_in_timestamp_order();
    void reports_open_and_decode_failures();
    void stop_clears_buffered_state();
    void restart_uses_a_fresh_source();
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

QTEST_GUILESS_MAIN(VideoFrameBufferTest)

#include "video_frame_buffer_test.moc"
