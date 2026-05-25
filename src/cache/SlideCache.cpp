#include "SlideCache.hpp"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logCache, "cache")

SlideCache::SlideCache(qint64 memoryLimitBytes)
    : m_memoryLimitBytes(memoryLimitBytes) {
}

bool SlideCache::contains(const SlideCacheKey& key) const {
    return m_entries.contains(key);
}

std::optional<QImage> SlideCache::get(const SlideCacheKey& key) {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        ++m_misses;
        qCDebug(logCache) << "cache miss" << key.pageIndex << key.pixelSize;
        return std::nullopt;
    }

    ++m_hits;
    touch(key);
    qCDebug(logCache) << "cache hit" << key.pageIndex << key.pixelSize;
    return it->image;
}

void SlideCache::put(const SlideCacheKey& key, const QImage& image) {
    if (image.isNull()) {
        return;
    }

    const qint64 bytes = estimateBytes(image);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        m_estimatedMemoryBytes -= it->bytes;
        it->image = image;
        it->bytes = bytes;
    } else {
        m_entries.insert(key, Entry{image, bytes});
    }

    m_estimatedMemoryBytes += bytes;
    touch(key);
    evictIfNeeded();

    qCDebug(logCache) << "cache put page" << key.pageIndex
                      << "bytes" << bytes
                      << "total" << m_estimatedMemoryBytes;
}

void SlideCache::clear() {
    m_entries.clear();
    m_lru.clear();
    m_estimatedMemoryBytes = 0;
    m_hits = 0;
    m_misses = 0;
    m_evictions = 0;
}

qint64 SlideCache::estimatedMemoryBytes() const {
    return m_estimatedMemoryBytes;
}

int SlideCache::hits() const {
    return m_hits;
}

int SlideCache::misses() const {
    return m_misses;
}

int SlideCache::evictions() const {
    return m_evictions;
}

qint64 SlideCache::estimateBytes(const QImage& image) {
    return qint64(image.width()) * qint64(image.height()) * 4ll;
}

void SlideCache::touch(const SlideCacheKey& key) {
    m_lru.removeAll(key);
    m_lru.prepend(key);
}

void SlideCache::evictIfNeeded() {
    while (m_estimatedMemoryBytes > m_memoryLimitBytes && !m_lru.isEmpty()) {
        const SlideCacheKey key = m_lru.takeLast();
        auto it = m_entries.find(key);
        if (it == m_entries.end()) {
            continue;
        }

        m_estimatedMemoryBytes -= it->bytes;
        m_entries.erase(it);
        ++m_evictions;
        qCDebug(logCache) << "cache eviction page" << key.pageIndex;
    }
}
