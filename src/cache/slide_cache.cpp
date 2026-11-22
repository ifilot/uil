#include "slide_cache.hpp"

#include <QLoggingCategory>

#include <algorithm>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(log_cache, "cache", QtInfoMsg)

SlideCache::SlideCache(qint64 memory_limit_bytes)
    : memory_limit_bytes_(memory_limit_bytes > 0
              ? memory_limit_bytes
              : recommended_memory_limit_bytes()) {
}

qint64 SlideCache::recommended_memory_limit_bytes() {
    constexpr qint64 default_cache_bytes = 512LL * 1024LL * 1024LL;
#if defined(Q_OS_WIN)
    constexpr qint64 minimum_cache_bytes = 128LL * 1024LL * 1024LL;
    constexpr qint64 maximum_cache_bytes = 1024LL * 1024LL * 1024LL;
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);
    if (GlobalMemoryStatusEx(&memory_status)) {
        return std::clamp(
            qint64(memory_status.ullTotalPhys / 8ULL),
            minimum_cache_bytes,
            maximum_cache_bytes);
    }
#endif
    return default_cache_bytes;
}

bool SlideCache::contains(const SlideCacheKey& key) const {
    return entries_.contains(key);
}

std::optional<QImage> SlideCache::get(const SlideCacheKey& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        ++misses_;
        qCDebug(log_cache) << "cache miss" << key.page_index << key.pixel_size;
        return std::nullopt;
    }

    ++hits_;
    touch(key);
    qCDebug(log_cache) << "cache hit" << key.page_index << key.pixel_size;
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

    qCDebug(log_cache) << "cache put page" << key.page_index
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

int SlideCache::remove_page(const QString& document_hash, int page_index) {
    int removed = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it.key().page_index == page_index && it.key().document_hash == document_hash) {
            estimated_memory_bytes_ -= it->bytes;
            lru_.removeAll(it.key());
            it = entries_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
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
    return qint64(image.sizeInBytes());
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
        qCDebug(log_cache) << "cache eviction page" << key.page_index;
    }
}
