#include "orbital/atomic_orbital_cache.hpp"

#include <QElapsedTimer>
#include <QTest>
#include <QTimer>

class AtomicOrbitalCacheTest final : public QObject {
  Q_OBJECT
 private slots:
  /** @brief Checks every geometry parameter and excludes presentation-only changes. */
  void geometry_identity();
  /** @brief Checks deduplication, shared immutable storage, and measured warm-cache speed. */
  void reuse_and_responsiveness();
  /** @brief Checks LRU eviction and delivery when a volume exceeds the cache budget. */
  void bounded_memory();
  /** @brief Checks replacement of speculative work and priority of newly visible geometry. */
  void navigation_replaces_pending_work();
  /** @brief Checks safe destruction with unfinished worker tasks. */
  void shutdown_with_pending_work();
};

void AtomicOrbitalCacheTest::geometry_identity() {
  AtomicOrbitalDefinition definition;
  const auto key = atomic_orbital_geometry_key(definition);
  auto themed = definition;
  themed.title = "Different title";
  themed.colormap = "garnet_slate";
  themed.positive_color = Qt::red;
  themed.negative_color = Qt::blue;
  themed.plane = AtomicOrbitalDefinition::Plane::YZ;
  themed.offset_initial = 0.5;
  themed.contour_levels = 5;
  QCOMPARE(atomic_orbital_geometry_key(themed), key);
  for (int field = 0; field < 5; ++field) {
    auto changed = definition;
    if (field == 0) ++changed.n;
    if (field == 1) ++changed.l;
    if (field == 2) ++changed.m;
    if (field == 3) changed.grid_size += 2;
    if (field == 4) changed.isovalue = 1e-10;
    QVERIFY(atomic_orbital_geometry_key(changed) != key);
  }
}

void AtomicOrbitalCacheTest::reuse_and_responsiveness() {
  AtomicOrbitalDefinition definition;
  definition.n = 5;
  definition.l = 4;
  definition.m = 0;
  definition.orbital = "5gz4";
  definition.grid_size = 81;
  QElapsedTimer timer;
  timer.start();
  const auto reference = build_atomic_orbital_volume(definition);
  const double cold_ms = timer.nsecsElapsed() / 1e6;
  QVERIFY(reference.is_valid());
  AtomicOrbitalCache cache;
  int completions = 0;
  connect(&cache, &AtomicOrbitalCache::volume_ready, this,
          [&](const QByteArray&, const AtomicOrbitalVolume& volume, const QString& error) {
            QVERIFY(error.isEmpty());
            QVERIFY(volume.is_valid());
            ++completions;
          });
  auto themed = definition;
  themed.colormap = "garnet_slate";
  timer.restart();
  cache.prepare({definition, themed}, {definition});
  const double request_ms = timer.nsecsElapsed() / 1e6;
  cache.prepare({themed, definition});
  QCOMPARE(cache.build_count(), quint64(1));
  int ticks = 0;
  QTimer heartbeat;
  connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
  heartbeat.start(1);
  QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 15000);
  QVERIFY(ticks > 0);
  const auto key = atomic_orbital_geometry_key(definition);
  const auto first = cache.get(key);
  QCOMPARE(first.values, reference.values);
  QCOMPARE(first.positive_vertices.size(), reference.positive_vertices.size());
  timer.restart();
  for (int i = 0; i < 10000; ++i) {
    const auto hit = cache.get(key);
    QVERIFY(hit.values.constData() == first.values.constData());
    QVERIFY(hit.positive_vertices.constData() == first.positive_vertices.constData());
  }
  const double hit_us = timer.nsecsElapsed() / 1e7;
  cache.prepare({definition}, {themed});
  QCOMPARE(cache.build_count(), quint64(1));
  qInfo(
      "81-cubed 5g: uncached build %.3f ms; asynchronous request %.3f ms; warm lookup %.3f us; UI "
      "ticks %d",
      cold_ms, request_ms, hit_us, ticks);
}

void AtomicOrbitalCacheTest::bounded_memory() {
  AtomicOrbitalDefinition first;
  first.grid_size = 33;
  auto second = first;
  second.n = 2;
  auto third = first;
  third.n = 3;
  const auto a = build_atomic_orbital_volume(first);
  const auto b = build_atomic_orbital_volume(second);
  const auto c = build_atomic_orbital_volume(third);
  // All s-orbitals use comparable allocations; budget holds any pair but not three.
  const qint64 budget =
      std::max({AtomicOrbitalCache::volume_bytes(a) + AtomicOrbitalCache::volume_bytes(b),
                AtomicOrbitalCache::volume_bytes(a) + AtomicOrbitalCache::volume_bytes(c),
                AtomicOrbitalCache::volume_bytes(b) + AtomicOrbitalCache::volume_bytes(c)});
  AtomicOrbitalCache cache(nullptr, budget);
  const auto ka = atomic_orbital_geometry_key(first);
  const auto kb = atomic_orbital_geometry_key(second);
  const auto kc = atomic_orbital_geometry_key(third);
  cache.prepare({first});
  QTRY_VERIFY(cache.get(ka).is_valid());
  cache.prepare({second});
  QTRY_VERIFY(cache.get(kb).is_valid());
  const auto retained = cache.get(ka);  // Touch a, making b the eviction candidate.
  cache.prepare({third});
  QTRY_VERIFY(cache.get(kc).is_valid());
  QVERIFY(cache.get(ka).is_valid());
  QVERIFY(!cache.get(kb).is_valid());
  QVERIFY(cache.size_bytes() <= budget);
  QVERIFY(retained.is_valid());

  AtomicOrbitalCache tiny(nullptr, 1);
  bool delivered = false;
  connect(&tiny, &AtomicOrbitalCache::volume_ready, this,
          [&](const QByteArray&, const AtomicOrbitalVolume& volume, const QString&) {
            delivered = volume.is_valid();
          });
  tiny.prepare({first});
  QTRY_VERIFY(delivered);
  QCOMPARE(tiny.size_bytes(), qint64(0));
  QVERIFY(!tiny.get(ka).is_valid());
}

void AtomicOrbitalCacheTest::navigation_replaces_pending_work() {
  QVector<AtomicOrbitalDefinition> definitions;
  for (int n = 1; n <= 5; ++n) {
    AtomicOrbitalDefinition definition;
    definition.n = n;
    definition.grid_size = 33;
    definitions.push_back(definition);
  }
  AtomicOrbitalCache cache;
  QVector<QByteArray> completed;
  connect(&cache, &AtomicOrbitalCache::volume_ready, this,
          [&](const QByteArray& key, const AtomicOrbitalVolume&, const QString&) {
            completed.push_back(key);
          });
  cache.prepare({}, definitions);   // First two start; the others remain pending.
  cache.prepare({definitions[4]});  // Jump: obsolete 3s/4s must never start.
  QTRY_COMPARE(completed.size(), 3);
  QCOMPARE(cache.build_count(), quint64(3));
  QVERIFY(completed.contains(atomic_orbital_geometry_key(definitions[4])));
  QVERIFY(!completed.contains(atomic_orbital_geometry_key(definitions[2])));
  QVERIFY(!completed.contains(atomic_orbital_geometry_key(definitions[3])));
}

void AtomicOrbitalCacheTest::shutdown_with_pending_work() {
  AtomicOrbitalDefinition definition;
  definition.grid_size = 33;
  {
    AtomicOrbitalCache cache;
    cache.prepare({definition});
  }
  QCoreApplication::processEvents();
}

QTEST_GUILESS_MAIN(AtomicOrbitalCacheTest)
#include "atomic_orbital_cache_test.moc"
