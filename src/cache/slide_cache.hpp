#pragma once

#include "slide_cache_key.hpp"

#include <QHash>
#include <QImage>
#include <QList>

#include <optional>

/**
 * @brief Memory-bounded LRU cache of rendered slide images.
 *
 * Not synchronized: use it from one thread (the GUI thread in AppController).
 */
class SlideCache {
public:
    /**
     * @brief Constructs a cache capped at @p memory_limit_bytes bytes.
     * @param[in] memory_limit_bytes Budget in bytes; a non-positive value selects
     *            recommended_memory_limit_bytes().
     */
    explicit SlideCache(qint64 memory_limit_bytes = -1);

    /** @brief Returns the default budget: 1/8 of physical memory within [128 MiB, 1 GiB] on
     * Windows, 512 MiB elsewhere. */
    static qint64 recommended_memory_limit_bytes();

    /** @brief Returns whether an image is cached for @p key. */
    bool contains(const SlideCacheKey& key) const;
    /** @brief Returns the cached image for @p key and updates cache statistics. */
    std::optional<QImage> get(const SlideCacheKey& key);
    /** @brief Inserts or replaces the image associated with @p key. */
    void put(const SlideCacheKey& key, const QImage& image);
    /** @brief Removes every cached image and resets cache statistics. */
    void clear();
    /**
     * @brief Removes every cached size of one page of one document.
     * @return Number of entries removed.
     */
    int remove_page(const QString& document_hash, int page_index);

    /** @brief Returns the estimated memory used by cached images. */
    qint64 estimated_memory_bytes() const;
    /** @brief Returns the number of successful cache lookups. */
    int hits() const;
    /** @brief Returns the number of unsuccessful cache lookups. */
    int misses() const;
    /** @brief Returns the number of entries evicted due to the memory limit. */
    int evictions() const;

private:
    struct Entry {
        QImage image;
        qint64 bytes = 0;
    };

    /** @brief Estimates the storage occupied by @p image. */
    static qint64 estimate_bytes(const QImage& image);
    /** @brief Marks @p key as the most recently used entry. */
    void touch(const SlideCacheKey& key);
    /** @brief Evicts least-recently-used entries until the cache is within its limit. */
    void evict_if_needed();

    qint64 memory_limit_bytes_ = 0;
    qint64 estimated_memory_bytes_ = 0;
    int hits_ = 0;
    int misses_ = 0;
    int evictions_ = 0;
    QHash<SlideCacheKey, Entry> entries_;
    QList<SlideCacheKey> lru_;
};
