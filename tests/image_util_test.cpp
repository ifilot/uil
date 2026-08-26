#include "util/image_util.hpp"

#include <QTest>

class ImageUtilTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Defines representative aspect-ratio fitting cases. */
    void contained_size_data();

    /** @brief Verifies that source dimensions are fitted without distortion. */
    void contained_size();

    /** @brief Verifies that invalid source or bounding dimensions are rejected. */
    void invalid_dimensions_return_empty_geometry();

    /** @brief Verifies that fitted images are centered within offset bounds. */
    void centered_rectangle_preserves_bounds_origin();
};

void ImageUtilTest::contained_size_data() {
    QTest::addColumn<QSizeF>("source_size");
    QTest::addColumn<QSize>("bounding_size");
    QTest::addColumn<QSize>("expected_size");

    QTest::newRow("exact fit") << QSizeF(1920, 1080) << QSize(1920, 1080)
                                << QSize(1920, 1080);
    QTest::newRow("wide into square") << QSizeF(16, 9) << QSize(100, 100) << QSize(100, 56);
    QTest::newRow("tall into square") << QSizeF(9, 16) << QSize(100, 100) << QSize(56, 100);
    QTest::newRow("square into wide") << QSizeF(1, 1) << QSize(320, 180) << QSize(180, 180);
    QTest::newRow("thin dimension remains visible") << QSizeF(1000, 1) << QSize(5, 5)
                                                     << QSize(5, 1);
}

void ImageUtilTest::contained_size() {
    QFETCH(QSizeF, source_size);
    QFETCH(QSize, bounding_size);
    QFETCH(QSize, expected_size);

    QCOMPARE(contained_size_for_aspect(source_size, bounding_size), expected_size);
}

void ImageUtilTest::invalid_dimensions_return_empty_geometry() {
    QVERIFY(contained_size_for_aspect(QSizeF(), QSize(100, 100)).isEmpty());
    QVERIFY(contained_size_for_aspect(QSizeF(16, 9), QSize()).isEmpty());
    QVERIFY(centered_rect_for_image(QSize(), QRect(10, 20, 100, 100)).isEmpty());
    QVERIFY(centered_rect_for_image(QSize(16, 9), QRect()).isEmpty());
}

void ImageUtilTest::centered_rectangle_preserves_bounds_origin() {
    QCOMPARE(centered_rect_for_image(QSize(16, 9), QRect(10, 20, 100, 100)),
             QRect(10, 42, 100, 56));
    QCOMPARE(centered_rect_for_image(QSize(9, 16), QRect(-20, -10, 100, 100)),
             QRect(2, -10, 56, 100));
}

QTEST_GUILESS_MAIN(ImageUtilTest)

#include "image_util_test.moc"
