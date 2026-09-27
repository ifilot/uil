#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include "orbital/atomic_orbital.hpp"

/** @brief Identifies geometry independently of titles, colors, and sampling controls. */
QByteArray atomic_orbital_geometry_key(const AtomicOrbitalDefinition& definition);

/** @brief GUI-thread LRU of immutable orbital geometry with bounded background preparation. */
class AtomicOrbitalCache final : public QObject {
  Q_OBJECT
 public:
  /** @brief Creates a cache with a 128 MiB default budget and two background workers. */
  explicit AtomicOrbitalCache(QObject* parent = nullptr, qint64 budget_bytes = 128 * 1024 * 1024);
  /** @brief Discards queued work and joins the at most two running builds. */
  ~AtomicOrbitalCache() override;
  /** @brief Returns shared geometry on a hit and marks it recently used; empty on a miss. */
  AtomicOrbitalVolume get(const QByteArray& key);
  /** @brief Replaces pending work; visible orbitals take precedence over speculative requests. */
  void prepare(const QVector<AtomicOrbitalDefinition>& visible,
               const QVector<AtomicOrbitalDefinition>& nearby = {});
  /** @brief Returns retained geometry bytes, excluding volumes held by active widgets/workers. */
  qint64 size_bytes() const;
  /** @brief Returns the number of actual geometry builds started, including speculative work. */
  quint64 build_count() const;
  /** @brief Accounts for the allocated arrays retained by a volume. */
  static qint64 volume_bytes(const AtomicOrbitalVolume& volume);

 signals:
  /** @brief Delivers a completed build on the GUI thread, including uncached oversized results. */
  void volume_ready(const QByteArray& key, const AtomicOrbitalVolume& volume, const QString& error);

 private:
  struct Entry {
    AtomicOrbitalVolume volume;
    qint64 bytes = 0;
    quint64 used = 0;
  };
  struct Request {
    QByteArray key;
    AtomicOrbitalDefinition definition;
  };
  /** @brief Starts the highest-priority pending work while worker slots are available. */
  void dispatch();
  /** @brief Inserts valid geometry and evicts least-recently-used entries to enforce the budget. */
  void insert(const QByteArray& key, const AtomicOrbitalVolume& volume);

  QThreadPool pool_;
  QHash<QByteArray, Entry> entries_;
  QSet<QByteArray> running_;
  QVector<Request> pending_;
  qint64 budget_bytes_;
  qint64 size_bytes_ = 0;
  quint64 clock_ = 0;
  quint64 build_count_ = 0;
};
