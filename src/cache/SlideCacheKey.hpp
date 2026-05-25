#pragma once

#include <QHash>
#include <QSize>
#include <QString>

struct SlideCacheKey {
    QString documentHash;
    int pageIndex = -1;
    QSize pixelSize;
    int rotation = 0;

    friend bool operator==(const SlideCacheKey& lhs, const SlideCacheKey& rhs) {
        return lhs.documentHash == rhs.documentHash
            && lhs.pageIndex == rhs.pageIndex
            && lhs.pixelSize == rhs.pixelSize
            && lhs.rotation == rhs.rotation;
    }
};

inline size_t qHash(const SlideCacheKey& key, size_t seed = 0) {
    seed = qHash(key.documentHash, seed);
    seed = qHash(key.pageIndex, seed);
    seed = qHash(key.pixelSize.width(), seed);
    seed = qHash(key.pixelSize.height(), seed);
    seed = qHash(key.rotation, seed);
    return seed;
}
