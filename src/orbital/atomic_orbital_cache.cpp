#include "orbital/atomic_orbital_cache.hpp"

#include <QDataStream>
#include <QIODevice>
#include <QRunnable>
#include <QThread>
#include <algorithm>

QByteArray atomic_orbital_geometry_key(const AtomicOrbitalDefinition& definition) {
  QByteArray key;
  QDataStream stream(&key, QIODevice::WriteOnly);
  stream << qint32(definition.n) << qint32(definition.l) << qint32(definition.m)
         << qint32(definition.grid_size)
         << (definition.isovalue == 0.0 ? 0.0 : definition.isovalue);
  return key;
}

AtomicOrbitalCache::AtomicOrbitalCache(QObject* parent, qint64 budget_bytes)
    : QObject(parent), budget_bytes_(std::max<qint64>(0, budget_bytes)) {
  pool_.setMaxThreadCount(2);
  pool_.setThreadPriority(QThread::LowPriority);
}

AtomicOrbitalCache::~AtomicOrbitalCache() {
  pool_.clear();
  pool_.waitForDone();
}

AtomicOrbitalVolume AtomicOrbitalCache::get(const QByteArray& key) {
  Q_ASSERT(QThread::currentThread() == thread());
  auto entry = entries_.find(key);
  if (entry == entries_.end()) return {};
  entry->used = ++clock_;
  return entry->volume;
}

void AtomicOrbitalCache::prepare(const QVector<AtomicOrbitalDefinition>& visible,
                                 const QVector<AtomicOrbitalDefinition>& nearby) {
  Q_ASSERT(QThread::currentThread() == thread());
  pending_.clear();
  QSet<QByteArray> requested;
  const auto append = [&](const QVector<AtomicOrbitalDefinition>& definitions) {
    for (const auto& definition : definitions) {
      if (!definition.is_valid()) continue;
      const auto key = atomic_orbital_geometry_key(definition);
      if (requested.contains(key)) continue;
      requested.insert(key);
      if (entries_.contains(key) || running_.contains(key)) continue;
      pending_.push_back({key, definition});
    }
  };
  append(visible);
  append(nearby);
  dispatch();
}

void AtomicOrbitalCache::dispatch() {
  while (running_.size() < pool_.maxThreadCount() && !pending_.isEmpty()) {
    const auto request = pending_.takeFirst();
    if (entries_.contains(request.key)) continue;
    running_.insert(request.key);
    ++build_count_;
    pool_.start(QRunnable::create([this, request] {
      QString error;
      const auto volume = build_atomic_orbital_volume(request.definition, &error);
      // The owning thread joins this pool before destroying the QObject. Qt discards
      // queued completions when the receiver dies, so neither workers nor callbacks
      // can access a destroyed cache.
      QMetaObject::invokeMethod(
          this,
          [this, key = request.key, volume, error] {
            running_.remove(key);
            if (volume.is_valid()) insert(key, volume);
            emit volume_ready(key, volume, error);
            dispatch();
          },
          Qt::QueuedConnection);
    }));
  }
}

void AtomicOrbitalCache::insert(const QByteArray& key, const AtomicOrbitalVolume& volume) {
  const qint64 bytes = volume_bytes(volume);
  if (bytes > budget_bytes_) return;
  while (size_bytes_ + bytes > budget_bytes_ && !entries_.isEmpty()) {
    auto oldest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->used < oldest->used) oldest = it;
    }
    size_bytes_ -= oldest->bytes;
    entries_.erase(oldest);
  }
  entries_.insert(key, {volume, bytes, ++clock_});
  size_bytes_ += bytes;
}

qint64 AtomicOrbitalCache::volume_bytes(const AtomicOrbitalVolume& volume) {
  return qint64(volume.values.capacity()) * sizeof(float) +
         qint64(volume.positive_vertices.capacity() + volume.negative_vertices.capacity()) *
             sizeof(AtomicOrbitalVertex);
}

qint64 AtomicOrbitalCache::size_bytes() const { return size_bytes_; }
quint64 AtomicOrbitalCache::build_count() const { return build_count_; }
