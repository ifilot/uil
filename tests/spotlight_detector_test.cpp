#include "util/spotlight_detector.hpp"

#include <QSignalSpy>
#include <QTest>

#include <memory>

class SpotlightDetectorTest final : public QObject {
    Q_OBJECT

private slots:
    void unavailable_without_probe();
    void reports_initial_presence();
    void emits_only_for_presence_transitions();
};

void SpotlightDetectorTest::unavailable_without_probe() {
    SpotlightDetector detector({}, 10);
    QVERIFY(!detector.detection_available());
    QVERIFY(!detector.is_present());
}

void SpotlightDetectorTest::reports_initial_presence() {
    SpotlightDetector detector([] { return true; }, 10);
    QVERIFY(detector.detection_available());
    QVERIFY(detector.is_present());
}

void SpotlightDetectorTest::emits_only_for_presence_transitions() {
    auto present = std::make_shared<bool>(false);
    SpotlightDetector detector([present] { return *present; }, 10);
    QSignalSpy spy(&detector, &SpotlightDetector::presence_changed);

    QTest::qWait(35);
    QCOMPARE(spy.size(), 0);

    *present = true;
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 250);
    QCOMPARE(spy.constFirst().constFirst().toBool(), true);
    QTest::qWait(35);
    QCOMPARE(spy.size(), 1);

    *present = false;
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 250);
    QCOMPARE(spy.constLast().constFirst().toBool(), false);
}

QTEST_GUILESS_MAIN(SpotlightDetectorTest)

#include "spotlight_detector_test.moc"
