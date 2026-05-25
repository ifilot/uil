#include "DocumentHash.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>

QString documentHashForFile(const QString& path) {
    const QFileInfo info(path);
    const QByteArray fingerprint = QStringLiteral("%1:%2:%3")
        .arg(info.absoluteFilePath())
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .toUtf8();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(fingerprint);
    return QString::fromLatin1(hash.result().toHex());
}
