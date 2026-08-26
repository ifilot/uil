#include "slide_cache.hpp"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logCache, "cache", QtInfoMsg)

SlideCache::SlideCache(qint64 memoryLimitBytes)
    : memory_limit_bytes_(memoryLimitBytes) {
}

bool SlideCache::contains(const SlideCacheKey& key) const {
    return entries_.contains(key);
}

std::optional<QImage> SlideCache::get(const SlideCacheKey& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        ++misses_;
        qCDebug(logCache) << "cache miss" << key.page_index << key.pixel_size;
        return std::nullopt;
    }

    ++hits_;
    touch(key);
    qCDebug(logCache) << "cache hit" << key.page_index << key.pixel_size;
    return it->image;
}

void SlideCache::put(const SlideCacheKey& key, const QImage& image) {
    if (image.isNull()) {
        return;
    }

    const qint64 bytes = estimate_bytes(image);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        estimated_memory_bytes_ -= it->bytes;
        it->image = image;
        it->bytes = bytes;
    } else {
        entries_.insert(key, Entry{image, bytes});
    }

    estimated_memory_bytes_ += bytes;
    touch(key);
    evict_if_needed();

    qCDebug(logCache) << "cache put page" << key.page_index
                      << "bytes" << bytes
                      << "total" << estimated_memory_bytes_;
}

void SlideCache::clear() {
    entries_.clear();
    lru_.clear();
    estimated_memory_bytes_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}

qint64 SlideCache::estimated_memory_bytes() const {
    return estimated_memory_bytes_;
}

int SlideCache::hits() const {
    return hits_;
}

int SlideCache::misses() const {
    return misses_;
}

int SlideCache::evictions() const {
    return evictions_;
}

qint64 SlideCache::estimate_bytes(const QImage& image) {
    return qint64(image.width()) * qint64(image.height()) * 4ll;
}

void SlideCache::touch(const SlideCacheKey& key) {
    lru_.removeAll(key);
    lru_.prepend(key);
}

void SlideCache::evict_if_needed() {
    while (estimated_memory_bytes_ > memory_limit_bytes_ && !lru_.isEmpty()) {
        const SlideCacheKey key = lru_.takeLast();
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            continue;
        }

        estimated_memory_bytes_ -= it->bytes;
        entries_.erase(it);
        ++evictions_;
        qCDebug(logCache) << "cache eviction page" << key.page_index;
    }
}
