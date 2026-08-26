#include "cache/slide_cache.hpp"

#include <QTest>

namespace {
/** @brief Creates a cache key for a test page. */
SlideCacheKey make_key(int page_index) {
    return SlideCacheKey{QStringLiteral("document"), page_index, QSize(1, 1), 0};
}

/** @brief Creates a non-null test image with predictable memory usage. */
QImage make_image(int width, int height, QRgb color) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}
}

class SlideCacheTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies hit and miss accounting for cache lookups. */
    void lookup_updates_statistics();

    /** @brief Verifies that replacing an entry updates its image and memory estimate. */
    void replacement_updates_memory_accounting();

    /** @brief Verifies least-recently-used eviction order. */
    void access_refreshes_lru_order();

    /** @brief Verifies that an individually oversized image is not retained. */
    void oversized_entry_is_evicted();

    /** @brief Verifies that null images are ignored. */
    void null_image_is_ignored();

    /** @brief Verifies that clearing removes entries and resets statistics. */
    void clear_resets_cache_state();
};

void SlideCacheTest::lookup_updates_statistics() {
    SlideCache cache(16);
    const SlideCacheKey key = make_key(0);
    const QImage image = make_image(1, 1, qRgb(10, 20, 30));
    cache.put(key, image);

    QVERIFY(cache.get(key).has_value());
    QCOMPARE(*cache.get(key), image);
    QVERIFY(!cache.get(make_key(1)).has_value());
    QCOMPARE(cache.hits(), 2);
    QCOMPARE(cache.misses(), 1);
}

void SlideCacheTest::replacement_updates_memory_accounting() {
    SlideCache cache(16);
    const SlideCacheKey key = make_key(0);
    cache.put(key, make_image(1, 1, qRgb(1, 2, 3)));
    QCOMPARE(cache.estimated_memory_bytes(), 4);

    const QImage replacement = make_image(2, 1, qRgb(4, 5, 6));
    cache.put(key, replacement);
    QCOMPARE(cache.estimated_memory_bytes(), 8);
    QCOMPARE(*cache.get(key), replacement);
    QCOMPARE(cache.evictions(), 0);
}

void SlideCacheTest::access_refreshes_lru_order() {
    SlideCache cache(8);
    const SlideCacheKey first_key = make_key(0);
    const SlideCacheKey second_key = make_key(1);
    const SlideCacheKey third_key = make_key(2);
    cache.put(first_key, make_image(1, 1, Qt::red));
    cache.put(second_key, make_image(1, 1, Qt::green));

    QVERIFY(cache.get(first_key).has_value());
    cache.put(third_key, make_image(1, 1, Qt::blue));

    QVERIFY(cache.contains(first_key));
    QVERIFY(!cache.contains(second_key));
    QVERIFY(cache.contains(third_key));
    QCOMPARE(cache.evictions(), 1);
}

void SlideCacheTest::oversized_entry_is_evicted() {
    SlideCache cache(4);
    const SlideCacheKey key = make_key(0);
    cache.put(key, make_image(2, 1, Qt::red));

    QVERIFY(!cache.contains(key));
    QCOMPARE(cache.estimated_memory_bytes(), 0);
    QCOMPARE(cache.evictions(), 1);
}

void SlideCacheTest::null_image_is_ignored() {
    SlideCache cache(16);
    cache.put(make_key(0), QImage());

    QCOMPARE(cache.estimated_memory_bytes(), 0);
    QCOMPARE(cache.evictions(), 0);
    QVERIFY(!cache.contains(make_key(0)));
}

void SlideCacheTest::clear_resets_cache_state() {
    SlideCache cache(16);
    cache.put(make_key(0), make_image(1, 1, Qt::red));
    QVERIFY(cache.get(make_key(0)).has_value());
    QVERIFY(!cache.get(make_key(1)).has_value());

    cache.clear();

    QVERIFY(!cache.contains(make_key(0)));
    QCOMPARE(cache.estimated_memory_bytes(), 0);
    QCOMPARE(cache.hits(), 0);
    QCOMPARE(cache.misses(), 0);
    QCOMPARE(cache.evictions(), 0);
}

QTEST_GUILESS_MAIN(SlideCacheTest)

#include "slide_cache_test.moc"
