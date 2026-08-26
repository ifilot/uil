#pragma once

#include <QHash>
#include <QSize>
#include <QString>

struct SlideCacheKey {
    QString document_hash;
    int page_index = -1;
    QSize pixel_size;
    int rotation = 0;

    /** @brief Compares two keys for exact cache identity. */
    friend bool operator==(const SlideCacheKey& lhs, const SlideCacheKey& rhs) {
        return lhs.document_hash == rhs.document_hash
            && lhs.page_index == rhs.page_index
            && lhs.pixel_size == rhs.pixel_size
            && lhs.rotation == rhs.rotation;
    }
};

/** @brief Computes the Qt hash value for @p key. */
inline size_t qHash(const SlideCacheKey& key, size_t seed = 0) {
    seed = qHash(key.document_hash, seed);
    seed = qHash(key.page_index, seed);
    seed = qHash(key.pixel_size.width(), seed);
    seed = qHash(key.pixel_size.height(), seed);
    seed = qHash(key.rotation, seed);
    return seed;
}
