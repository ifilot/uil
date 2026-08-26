#include "render/render_scheduler.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QTest>
#include <QWaitCondition>

#include <memory>

namespace {
struct BlockingRenderState {
    QMutex mutex;
    QWaitCondition changed;
    QList<int> started_pages;
    bool blocker_started = false;
    bool blocker_released = false;
    bool blocker_finished = false;
};

/** @brief Creates a valid synthetic render request for one page. */
RenderRequest make_request(int page_index, int generation) {
    return RenderRequest{
        QStringLiteral("synthetic.pdf"),
        QStringLiteral("document-hash"),
        page_index,
        QSize(16, 9),
        0,
        generation};
}

/** @brief Returns a non-null image for a synthetic render request. */
QImage synthetic_image() {
    QImage image(16, 9, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    return image;
}
}  // namespace

class RenderSchedulerTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies promotion of already-pending work ahead of lower priorities. */
    void pending_request_can_be_promoted();

    /** @brief Verifies generation changes discard active and queued stale results. */
    void clear_discards_stale_work();
};

void RenderSchedulerTest::pending_request_can_be_promoted() {
    auto state = std::make_shared<BlockingRenderState>();
    RenderScheduler scheduler(
        nullptr,
        [state](const RenderRequest& request, QString*) {
            QMutexLocker locker(&state->mutex);
            state->started_pages.append(request.page_index);
            state->changed.wakeAll();
            if (request.page_index == 0) {
                state->blocker_started = true;
                state->changed.wakeAll();
                while (!state->blocker_released) {
                    state->changed.wait(&state->mutex);
                }
                state->blocker_finished = true;
                state->changed.wakeAll();
            }
            locker.unlock();
            return synthetic_image();
        },
        1);

    const int generation = scheduler.generation();
    scheduler.request_render(make_request(0, generation), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(([&state] {
        QMutexLocker locker(&state->mutex);
        return state->blocker_started;
    })(), 5000);

    scheduler.request_render(make_request(1, generation), 10);
    scheduler.request_render(make_request(2, generation), 100);
    scheduler.request_render(make_request(1, generation), 200);
    {
        QMutexLocker locker(&state->mutex);
        state->blocker_released = true;
        state->changed.wakeAll();
    }

    QTRY_VERIFY_WITH_TIMEOUT(([&state] {
        QMutexLocker locker(&state->mutex);
        return state->started_pages.size() == 3;
    })(), 5000);
    QMutexLocker locker(&state->mutex);
    QCOMPARE(state->started_pages, QList<int>({0, 1, 2}));
}

void RenderSchedulerTest::clear_discards_stale_work() {
    auto state = std::make_shared<BlockingRenderState>();
    int delivered_results = 0;
    RenderScheduler scheduler(
        nullptr,
        [state](const RenderRequest&, QString*) {
            QMutexLocker locker(&state->mutex);
            state->blocker_started = true;
            state->changed.wakeAll();
            while (!state->blocker_released) {
                state->changed.wait(&state->mutex);
            }
            state->blocker_finished = true;
            state->changed.wakeAll();
            locker.unlock();
            return synthetic_image();
        },
        1);
    connect(&scheduler, &RenderScheduler::render_finished, this,
            [&delivered_results](const RenderRequest&, const QImage&, qint64, const QString&) {
                ++delivered_results;
            });

    const int generation = scheduler.generation();
    scheduler.request_render(make_request(0, generation), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(([&state] {
        QMutexLocker locker(&state->mutex);
        return state->blocker_started;
    })(), 5000);
    scheduler.request_render(make_request(1, generation), 500);
    scheduler.clear();
    {
        QMutexLocker locker(&state->mutex);
        state->blocker_released = true;
        state->changed.wakeAll();
    }

    QTRY_VERIFY_WITH_TIMEOUT(([&state] {
        QMutexLocker locker(&state->mutex);
        return state->blocker_finished;
    })(), 5000);
    QTest::qWait(100);
    QCOMPARE(delivered_results, 0);
}

QTEST_GUILESS_MAIN(RenderSchedulerTest)

#include "render_scheduler_test.moc"
