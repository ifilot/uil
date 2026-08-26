#pragma once

#include <QString>

/** @brief Computes a stable SHA-256 hash for the file at @p path. */
QString document_hash_for_file(const QString& path);
