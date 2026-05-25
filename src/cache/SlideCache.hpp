#pragma once

#include "SlideCacheKey.hpp"

#include <QHash>
#include <QImage>
#include <QList>

#include <optional>

class SlideCache {
public:
    explicit SlideCache(qint64 memoryLimitBytes = 512ll * 1024ll * 1024ll);

    bool contains(const SlideCacheKey& key) const;
    std::optional<QImage> get(const SlideCacheKey& key);
    void put(const SlideCacheKey& key, const QImage& image);
    void clear();

    qint64 estimatedMemoryBytes() const;
    int hits() const;
    int misses() const;
    int evictions() const;

private:
    struct Entry {
        QImage image;
        qint64 bytes = 0;
    };

    static qint64 estimateBytes(const QImage& image);
    void touch(const SlideCacheKey& key);
    void evictIfNeeded();

    qint64 m_memoryLimitBytes = 0;
    qint64 m_estimatedMemoryBytes = 0;
    int m_hits = 0;
    int m_misses = 0;
    int m_evictions = 0;
    QHash<SlideCacheKey, Entry> m_entries;
    QList<SlideCacheKey> m_lru;
};
