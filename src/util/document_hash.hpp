#pragma once

#include <QString>

/**
 * @brief Computes a cheap version fingerprint for the file at @p path.
 *
 * The result is the lowercase hexadecimal SHA-256 digest of the string
 * `absolute_path:size:modification_time_ms`; the file contents are never read. It identifies
 * one file at one location and revision, which is sufficient for keying per-document caches.
 * It is not a content hash: identical copies at different paths produce different
 * fingerprints, and an edit that preserves both the size and the modification time is not
 * detected. A missing file still yields a fingerprint.
 *
 * @param path File path; relative paths are resolved against the current directory.
 * @return 64-character hexadecimal fingerprint.
 */
QString document_hash_for_file(const QString& path);
