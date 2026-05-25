#include "PdfMediaDetector.hpp"

#include "media/VideoFrameExtractor.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <optional>
#include <unordered_set>
#include <utility>

#include <zlib.h>

Q_LOGGING_CATEGORY(logMedia, "pdf.media")

namespace {
using ObjectMap = QHash<int, QByteArray>;

QByteArray inflateFlateData(const QByteArray& input) {
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());

    if (inflateInit(&stream) != Z_OK) {
        return {};
    }

    QByteArray output;
    std::array<char, 32768> buffer{};
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = uInt(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        output.append(buffer.data(), int(buffer.size() - stream.avail_out));
    }

    inflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray{};
}

int streamDataStart(const QByteArray& objectBody, int streamKeywordIndex) {
    int start = streamKeywordIndex + int(QByteArray("stream").size());
    if (objectBody.mid(start, 2) == "\r\n") {
        return start + 2;
    }
    if (start < objectBody.size() && (objectBody.at(start) == '\n' || objectBody.at(start) == '\r')) {
        return start + 1;
    }
    return start;
}

QByteArray dictionaryPart(const QByteArray& objectBody) {
    const int streamIndex = objectBody.indexOf("stream");
    return streamIndex >= 0 ? objectBody.left(streamIndex) : objectBody;
}

std::optional<int> integerValue(const QByteArray& bytes, const QString& key) {
    const QRegularExpression expression(QStringLiteral(R"(/%1\s+(\d+))").arg(key));
    const QRegularExpressionMatch match = expression.match(QString::fromLatin1(bytes));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toInt();
}

bool hasName(const QByteArray& bytes, const QString& name) {
    const QRegularExpression expression(QStringLiteral(R"(/%1\b)").arg(name));
    return expression.match(QString::fromLatin1(bytes)).hasMatch();
}

bool hasType(const QByteArray& bytes, const QString& typeName) {
    const QRegularExpression expression(QStringLiteral(R"(/Type\s*/%1\b)").arg(typeName));
    return expression.match(QString::fromLatin1(bytes)).hasMatch();
}

std::optional<int> referencedObject(const QByteArray& bytes, const QString& key) {
    const QRegularExpression expression(QStringLiteral(R"(/%1\s+(\d+)\s+\d+\s+R)").arg(key));
    const QRegularExpressionMatch match = expression.match(QString::fromLatin1(bytes));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toInt();
}

QVector<int> referencedObjects(QString text) {
    QVector<int> refs;
    const QRegularExpression expression(QStringLiteral(R"((\d+)\s+\d+\s+R)"));
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    while (it.hasNext()) {
        refs.push_back(it.next().captured(1).toInt());
    }
    return refs;
}

QString bracketedValue(const QByteArray& bytes, const QString& key) {
    const QString text = QString::fromLatin1(bytes);
    const int keyIndex = text.indexOf(QStringLiteral("/") + key);
    if (keyIndex < 0) {
        return {};
    }

    const int begin = text.indexOf(QLatin1Char('['), keyIndex);
    const int end = text.indexOf(QLatin1Char(']'), begin + 1);
    if (begin < 0 || end < 0) {
        return {};
    }
    return text.mid(begin + 1, end - begin - 1);
}

QString pdfStringValue(const QByteArray& bytes, const QString& key) {
    const QString text = QString::fromLatin1(bytes);
    const int keyIndex = text.indexOf(QStringLiteral("/") + key);
    if (keyIndex < 0) {
        return {};
    }

    const int begin = text.indexOf(QLatin1Char('('), keyIndex);
    if (begin < 0) {
        return {};
    }

    QString result;
    int depth = 1;
    bool escaped = false;
    for (int i = begin + 1; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (escaped) {
            result.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char('(')) {
            ++depth;
        } else if (ch == QLatin1Char(')')) {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        result.append(ch);
    }
    return result;
}

QString subtypeForAnnotation(const QByteArray& bytes) {
    const QRegularExpression expression(QStringLiteral(R"(/Subtype\s*/([A-Za-z0-9]+)\b)"));
    const QRegularExpressionMatch match = expression.match(QString::fromLatin1(bytes));
    return match.hasMatch() ? match.captured(1) : QStringLiteral("Media");
}

QRectF rectForAnnotation(const QByteArray& bytes) {
    const QString rectText = bracketedValue(bytes, QStringLiteral("Rect"));
    if (rectText.isEmpty()) {
        return {};
    }

    const QStringList parts = rectText.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 4) {
        return {};
    }

    const double x1 = parts.at(0).toDouble();
    const double y1 = parts.at(1).toDouble();
    const double x2 = parts.at(2).toDouble();
    const double y2 = parts.at(3).toDouble();
    return QRectF(QPointF(std::min(x1, x2), std::min(y1, y2)),
                  QPointF(std::max(x1, x2), std::max(y1, y2)));
}

QString mediaFileName(const QByteArray& bytes) {
    for (const QString& key : {QStringLiteral("UF"), QStringLiteral("F"), QStringLiteral("DOS"), QStringLiteral("Mac"), QStringLiteral("Unix")}) {
        const QString value = pdfStringValue(bytes, key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

bool isMediaAnnotation(const QByteArray& bytes) {
    return hasName(bytes, QStringLiteral("Movie"))
        || hasName(bytes, QStringLiteral("RichMedia"))
        || hasName(bytes, QStringLiteral("Rendition"))
        || hasName(bytes, QStringLiteral("EmbeddedFile"))
        || hasName(bytes, QStringLiteral("Sound"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype/Movie"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype /Movie"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype/RichMedia"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype /RichMedia"));
}

ObjectMap extractIndirectObjects(const QByteArray& pdfBytes) {
    ObjectMap objects;
    const QString text = QString::fromLatin1(pdfBytes);
    const QRegularExpression expression(QStringLiteral(R"((\d+)\s+\d+\s+obj\b)"));
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int objectNumber = match.captured(1).toInt();
        const qsizetype bodyStart = match.capturedEnd(0);
        const qsizetype end = pdfBytes.indexOf("endobj", bodyStart);
        if (end < 0) {
            continue;
        }
        objects.insert(objectNumber, pdfBytes.mid(bodyStart, end - bodyStart).trimmed());
    }
    return objects;
}

void extractObjectStreams(ObjectMap& objects) {
    QVector<std::pair<int, QByteArray>> objectStreams;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (hasType(dictionaryPart(it.value()), QStringLiteral("ObjStm"))) {
            objectStreams.push_back({it.key(), it.value()});
        }
    }

    for (const auto& [objectStreamNumber, objectBody] : objectStreams) {
        const QByteArray dictionary = dictionaryPart(objectBody);
        if (!hasName(dictionary, QStringLiteral("FlateDecode"))) {
            continue;
        }

        const std::optional<int> count = integerValue(dictionary, QStringLiteral("N"));
        const std::optional<int> first = integerValue(dictionary, QStringLiteral("First"));
        if (!count || !first) {
            continue;
        }

        const int streamIndex = objectBody.indexOf("stream");
        const int endStreamIndex = objectBody.lastIndexOf("endstream");
        if (streamIndex < 0 || endStreamIndex <= streamIndex) {
            continue;
        }

        const int dataStart = streamDataStart(objectBody, streamIndex);
        const QByteArray decoded = inflateFlateData(objectBody.mid(dataStart, endStreamIndex - dataStart));
        if (decoded.isEmpty() || decoded.size() <= *first) {
            qCWarning(logMedia) << "Could not decode object stream" << objectStreamNumber;
            continue;
        }

        const QString header = QString::fromLatin1(decoded.left(*first));
        const QRegularExpression pairExpression(QStringLiteral(R"((\d+)\s+(\d+))"));
        QRegularExpressionMatchIterator pairIt = pairExpression.globalMatch(header);
        QVector<std::pair<int, int>> pairs;
        while (pairIt.hasNext() && pairs.size() < *count) {
            const QRegularExpressionMatch match = pairIt.next();
            pairs.push_back({match.captured(1).toInt(), match.captured(2).toInt()});
        }

        std::sort(pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second < rhs.second;
        });

        for (int i = 0; i < pairs.size(); ++i) {
            const int objectNumber = pairs.at(i).first;
            const int objectStart = *first + pairs.at(i).second;
            const int objectEnd = (i + 1 < pairs.size()) ? *first + pairs.at(i + 1).second : decoded.size();
            if (objectStart >= *first && objectEnd > objectStart && objectEnd <= decoded.size()) {
                objects.insert(objectNumber, decoded.mid(objectStart, objectEnd - objectStart).trimmed());
            }
        }
    }
}

QVector<int> pageOrderFromNode(const ObjectMap& objects, int objectNumber, std::unordered_set<int>& visited) {
    if (visited.contains(objectNumber)) {
        return {};
    }
    visited.insert(objectNumber);

    const QByteArray body = objects.value(objectNumber);
    if (body.isEmpty()) {
        return {};
    }

    if (hasType(body, QStringLiteral("Page"))) {
        return {objectNumber};
    }

    QVector<int> pages;
    if (hasType(body, QStringLiteral("Pages"))) {
        const QString kids = bracketedValue(body, QStringLiteral("Kids"));
        for (int child : referencedObjects(kids)) {
            pages += pageOrderFromNode(objects, child, visited);
        }
    }
    return pages;
}

QVector<int> orderedPageObjects(const ObjectMap& objects) {
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!hasType(it.value(), QStringLiteral("Catalog"))) {
            continue;
        }

        const std::optional<int> pagesRoot = referencedObject(it.value(), QStringLiteral("Pages"));
        if (!pagesRoot) {
            continue;
        }

        std::unordered_set<int> visited;
        QVector<int> pages = pageOrderFromNode(objects, *pagesRoot, visited);
        if (!pages.isEmpty()) {
            return pages;
        }
    }

    QVector<int> fallback;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (hasType(it.value(), QStringLiteral("Page"))) {
            fallback.push_back(it.key());
        }
    }
    std::sort(fallback.begin(), fallback.end());
    return fallback;
}

QVector<int> annotationObjectsForPage(const ObjectMap& objects, const QByteArray& pageBody) {
    if (const std::optional<int> annotsRef = referencedObject(pageBody, QStringLiteral("Annots"))) {
        return referencedObjects(QString::fromLatin1(objects.value(*annotsRef)));
    }

    const QString directAnnots = bracketedValue(pageBody, QStringLiteral("Annots"));
    return referencedObjects(directAnnots);
}

PdfMediaAnnotation annotationFromObject(int pageIndex, int objectNumber, const QByteArray& body) {
    return PdfMediaAnnotation{
        pageIndex,
        objectNumber,
        subtypeForAnnotation(body),
        mediaFileName(body),
        QString(),
        rectForAnnotation(body),
        QImage()
    };
}

QString normalizedPackagePath(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(path);
}

QString resolveMediaPath(
    const QString& pdfPath,
    const QString& fileName,
    const QString& packageRootPath,
    const QStringList& packageMovieAssetPaths) {
    if (fileName.isEmpty()) {
        return {};
    }

    QFileInfo mediaInfo(fileName);
    if (mediaInfo.isAbsolute()) {
        return mediaInfo.absoluteFilePath();
    }

    const QString normalizedTarget = normalizedPackagePath(fileName);
    if (!packageRootPath.isEmpty()) {
        for (const QString& assetPath : packageMovieAssetPaths) {
            if (normalizedTarget == normalizedPackagePath(assetPath)) {
                return QFileInfo(QDir(packageRootPath), normalizedTarget).absoluteFilePath();
            }
        }
    }

    const QFileInfo pdfInfo(pdfPath);
    return QFileInfo(pdfInfo.absoluteDir(), fileName).absoluteFilePath();
}

void resolveAndExtractMediaFrames(
    PdfMediaScanResult& result,
    const QString& pdfPath,
    const QString& packageRootPath,
    const QStringList& packageMovieAssetPaths) {
    for (PdfMediaAnnotation& annotation : result.annotations) {
        annotation.resolvedFilePath = resolveMediaPath(pdfPath, annotation.fileName, packageRootPath, packageMovieAssetPaths);
        if (annotation.isMp4()) {
            QString errorMessage;
            annotation.firstFrame = extractFirstVideoFrame(annotation.resolvedFilePath, &errorMessage);
            if (!errorMessage.isEmpty()) {
                qCInfo(logMedia) << "Could not extract first MP4 frame from" << annotation.resolvedFilePath << errorMessage;
            }
        }
    }
}
}

bool PdfMediaAnnotation::hasFirstFrame() const {
    return !firstFrame.isNull();
}

bool PdfMediaAnnotation::isMp4() const {
    return resolvedFilePath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive);
}

bool PdfMediaScanResult::hasMedia() const {
    return !annotations.isEmpty();
}

QString PdfMediaScanResult::summary() const {
    if (annotations.isEmpty()) {
        return QStringLiteral("No PDF media annotations detected");
    }

    QStringList parts;
    for (const PdfMediaAnnotation& annotation : annotations) {
        QString part = annotation.pageIndex >= 0
            ? QStringLiteral("page %1").arg(annotation.pageIndex + 1)
            : QStringLiteral("unknown page");
        part += QStringLiteral(" %1").arg(annotation.subtype);
        if (!annotation.fileName.isEmpty()) {
            part += QStringLiteral(" (%1)").arg(annotation.fileName);
        }
        if (annotation.hasFirstFrame()) {
            part += QStringLiteral(" [first frame ready]");
        }
        parts.push_back(part);
    }
    return QStringLiteral("%1 media annotation(s): %2").arg(annotations.size()).arg(parts.join(QStringLiteral("; ")));
}

PdfMediaScanResult scanPdfMediaAnnotations(
    const QString& path,
    const QString& packageRootPath,
    const QStringList& packageMovieAssetPaths) {
    PdfMediaScanResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logMedia) << "Could not scan PDF media annotations; failed to open" << path;
        return result;
    }

    ObjectMap objects = extractIndirectObjects(file.readAll());
    extractObjectStreams(objects);

    std::unordered_set<int> seenMediaObjects;
    const QVector<int> pages = orderedPageObjects(objects);
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const QByteArray pageBody = objects.value(pages.at(pageIndex));
        for (int annotationObject : annotationObjectsForPage(objects, pageBody)) {
            const QByteArray annotationBody = objects.value(annotationObject);
            if (!annotationBody.isEmpty() && isMediaAnnotation(annotationBody)) {
                result.annotations.push_back(annotationFromObject(pageIndex, annotationObject, annotationBody));
                seenMediaObjects.insert(annotationObject);
            }
        }
    }

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (seenMediaObjects.contains(it.key()) || !isMediaAnnotation(it.value())) {
            continue;
        }
        result.annotations.push_back(annotationFromObject(-1, it.key(), it.value()));
    }

    resolveAndExtractMediaFrames(result, path, packageRootPath, packageMovieAssetPaths);
    qCInfo(logMedia) << result.summary();
    return result;
}
