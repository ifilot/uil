#include "pdf_media_detector.hpp"

#include "media/video_frame_extractor.hpp"
#include "util/performance_log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <unordered_set>
#include <utility>

#include <zlib.h>

Q_LOGGING_CATEGORY(logMedia, "pdf.media")

namespace {
using ObjectMap = QHash<int, QByteArray>;
constexpr qint64 kMaximumPdfScanBytes = 512LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumInflatedObjectStreamBytes = 64 * 1024 * 1024;
constexpr qsizetype kMaximumTotalInflatedObjectStreamBytes = 256 * 1024 * 1024;
constexpr qsizetype kMaximumEmbeddedFigureBytes = 4 * 1024 * 1024;

/** @brief Inflates PDF stream data encoded with the Flate filter. */
QByteArray inflate_flate_data(const QByteArray& input) {
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
        if (output.size() > kMaximumInflatedObjectStreamBytes) {
            inflateEnd(&stream);
            return {};
        }
    }

    inflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray{};
}

/** @brief Returns the first payload byte following a PDF stream keyword. */
int stream_data_start(const QByteArray& object_body, int stream_keyword_index) {
    int start = stream_keyword_index + int(QByteArray("stream").size());
    if (object_body.mid(start, 2) == "\r\n") {
        return start + 2;
    }
    if (start < object_body.size() && (object_body.at(start) == '\n' || object_body.at(start) == '\r')) {
        return start + 1;
    }
    return start;
}

/** @brief Returns the dictionary portion of a PDF indirect object. */
QByteArray dictionary_part(const QByteArray& object_body) {
    const int streamIndex = object_body.indexOf("stream");
    return streamIndex >= 0 ? object_body.left(streamIndex) : object_body;
}

/** @brief Reads an integer associated with a PDF dictionary key. */
std::optional<int> integer_value(const QByteArray& bytes, const QString& key) {
    const QRegularExpression expression(QStringLiteral(R"(/%1\s+(\d+))").arg(key));
    const QRegularExpressionMatch match = expression.match(QString::fromLatin1(bytes));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toInt();
}

/** @brief Returns whether PDF bytes contain a particular name object. */
bool has_name(const QByteArray& bytes, const QString& name) {
    const QRegularExpression expression(QStringLiteral(R"(/%1\b)").arg(name));
    return expression.match(QString::fromLatin1(bytes)).hasMatch();
}

/** @brief Returns whether a PDF dictionary declares a particular type. */
bool has_type(const QByteArray& bytes, const QString& type_name) {
    const QRegularExpression expression(QStringLiteral(R"(/Type\s*/%1\b)").arg(type_name));
    return expression.match(QString::fromLatin1(bytes)).hasMatch();
}

/** @brief Reads one indirect-object reference associated with a dictionary key. */
std::optional<int> referenced_object(const QByteArray& bytes, const QString& key) {
    const QRegularExpression expression(QStringLiteral(R"(/%1\s+(\d+)\s+\d+\s+R)").arg(key));
    const QRegularExpressionMatch match = expression.match(QString::fromLatin1(bytes));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toInt();
}

/** @brief Extracts all indirect-object references from PDF source text. */
QVector<int> referenced_objects(QString text) {
    QVector<int> refs;
    const QRegularExpression expression(QStringLiteral(R"((\d+)\s+\d+\s+R)"));
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    while (it.hasNext()) {
        refs.push_back(it.next().captured(1).toInt());
    }
    return refs;
}

/** @brief Reads a bracketed PDF string associated with a dictionary key. */
QString bracketed_value(const QByteArray& bytes, const QString& key) {
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

/** @brief Decodes a PDF byte string, including UTF-16 byte-order marks. */
QString decoded_pdf_string_bytes(const QByteArray& decoded) {
    if (decoded.size() >= 2
        && ((uchar(decoded.at(0)) == 0xfe && uchar(decoded.at(1)) == 0xff)
            || (uchar(decoded.at(0)) == 0xff && uchar(decoded.at(1)) == 0xfe))) {
        const bool big_endian = uchar(decoded.at(0)) == 0xfe;
        QString result;
        result.reserve((decoded.size() - 2) / 2);
        for (int i = 2; i + 1 < decoded.size(); i += 2) {
            const ushort first = uchar(decoded.at(i));
            const ushort second = uchar(decoded.at(i + 1));
            result.append(QChar(big_endian ? ushort((first << 8) | second)
                                           : ushort(first | (second << 8))));
        }
        return result;
    }
    return QString::fromLatin1(decoded);
}

/** @brief Reads and decodes a PDF string associated with a dictionary key. */
QString pdf_string_value(const QByteArray& bytes, const QString& key) {
    const QString text = QString::fromLatin1(bytes);
    const QRegularExpression key_expression(
        QStringLiteral(R"(/%1\b)").arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch key_match = key_expression.match(text);
    if (!key_match.hasMatch()) {
        return {};
    }

    int begin = key_match.capturedEnd();
    while (begin < text.size() && text.at(begin).isSpace()) {
        ++begin;
    }
    if (begin >= text.size()) {
        return {};
    }

    if (text.at(begin) == QLatin1Char('<')
        && begin + 1 < text.size()
        && text.at(begin + 1) != QLatin1Char('<')) {
        const int end = text.indexOf(QLatin1Char('>'), begin + 1);
        if (end < 0) {
            return {};
        }
        QByteArray hex = text.mid(begin + 1, end - begin - 1).toLatin1();
        hex.removeIf([](char ch) { return std::isspace(static_cast<unsigned char>(ch)); });
        return decoded_pdf_string_bytes(QByteArray::fromHex(hex));
    }

    if (text.at(begin) != QLatin1Char('(')) {
        return {};
    }

    QByteArray decoded;
    int depth = 1;
    for (int i = begin + 1; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\\')) {
            if (++i >= text.size()) {
                break;
            }
            const QChar escaped = text.at(i);
            if (escaped >= QLatin1Char('0') && escaped <= QLatin1Char('7')) {
                int value = escaped.unicode() - '0';
                int digits = 1;
                while (digits < 3 && i + 1 < text.size()
                       && text.at(i + 1) >= QLatin1Char('0')
                       && text.at(i + 1) <= QLatin1Char('7')) {
                    value = value * 8 + text.at(++i).unicode() - '0';
                    ++digits;
                }
                decoded.append(char(value & 0xff));
            } else if (escaped == QLatin1Char('n')) {
                decoded.append('\n');
            } else if (escaped == QLatin1Char('r')) {
                decoded.append('\r');
            } else if (escaped == QLatin1Char('t')) {
                decoded.append('\t');
            } else if (escaped == QLatin1Char('b')) {
                decoded.append('\b');
            } else if (escaped == QLatin1Char('f')) {
                decoded.append('\f');
            } else if (escaped == QLatin1Char('\r') || escaped == QLatin1Char('\n')) {
                if (escaped == QLatin1Char('\r') && i + 1 < text.size()
                    && text.at(i + 1) == QLatin1Char('\n')) {
                    ++i;
                }
            } else {
                decoded.append(escaped.toLatin1());
            }
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
        decoded.append(ch.toLatin1());
    }
    return decoded_pdf_string_bytes(decoded);
}

/** @brief Returns the subtype declared by a PDF annotation dictionary. */
QString subtype_for_annotation(const QByteArray& bytes) {
    const QRegularExpression expression(QStringLiteral(R"(/Subtype\s*/([A-Za-z0-9]+)\b)"));
    const QRegularExpressionMatch match = expression.match(QString::fromLatin1(bytes));
    return match.hasMatch() ? match.captured(1) : QStringLiteral("Media");
}

/** @brief Reads an annotation rectangle from a PDF dictionary. */
QRectF rect_for_annotation(const QByteArray& bytes) {
    const QString rectText = bracketed_value(bytes, QStringLiteral("Rect"));
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

/** @brief Extracts a referenced media filename from annotation data. */
QString media_file_name(const QByteArray& bytes) {
    for (const QString& key : {QStringLiteral("UF"), QStringLiteral("F"), QStringLiteral("DOS"), QStringLiteral("Mac"), QStringLiteral("Unix")}) {
        const QString value = pdf_string_value(bytes, key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

/** @brief Returns whether a PDF annotation can contain playable media. */
bool is_media_annotation(const QByteArray& bytes) {
    if (has_type(dictionary_part(bytes), QStringLiteral("EmbeddedFile"))) {
        return false;
    }
    return has_name(bytes, QStringLiteral("Movie"))
        || has_name(bytes, QStringLiteral("RichMedia"))
        || has_name(bytes, QStringLiteral("Rendition"))
        || has_name(bytes, QStringLiteral("EmbeddedFile"))
        || has_name(bytes, QStringLiteral("Sound"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype/Movie"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype /Movie"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype/RichMedia"))
        || QString::fromLatin1(bytes).contains(QStringLiteral("/Subtype /RichMedia"));
}

/** @brief Returns whether a PDF annotation describes a UIL molecule. */
bool is_molecule_annotation(const QByteArray& bytes) {
    return has_name(bytes, QStringLiteral("UILMolecule"));
}

/** @brief Returns whether a PDF annotation describes an embedded UIL interactive figure. */
bool is_interactive_figure_annotation(const QByteArray& bytes) {
    return has_name(bytes, QStringLiteral("UILInteractiveFigure"));
}

/** @brief Cheaply detects whether object bytes could contain a supported media annotation. */
bool contains_media_marker(const QByteArray& bytes) {
    return bytes.contains("/Movie")
        || bytes.contains("/RichMedia")
        || bytes.contains("/Rendition")
        || bytes.contains("/EmbeddedFile")
        || bytes.contains("/Sound")
        || bytes.contains("/UILMolecule")
        || bytes.contains("/UILInteractiveFigure");
}

/** @brief Extracts directly represented indirect objects from PDF bytes. */
ObjectMap extract_indirect_objects(const QByteArray& pdf_bytes) {
    ObjectMap objects;
    const QString text = QString::fromLatin1(pdf_bytes);
    const QRegularExpression expression(QStringLiteral(R"((\d+)\s+\d+\s+obj\b)"));
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int object_number = match.captured(1).toInt();
        const qsizetype bodyStart = match.capturedEnd(0);
        const qsizetype end = pdf_bytes.indexOf("endobj", bodyStart);
        if (end < 0) {
            continue;
        }
        objects.insert(object_number, pdf_bytes.mid(bodyStart, end - bodyStart).trimmed());
    }
    return objects;
}

/** @brief Expands compressed PDF object streams into the object map. */
void extract_object_streams(ObjectMap& objects) {
    QVector<std::pair<int, QByteArray>> objectStreams;
    qsizetype total_inflated_bytes = 0;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (has_type(dictionary_part(it.value()), QStringLiteral("ObjStm"))) {
            objectStreams.push_back({it.key(), it.value()});
        }
    }

    for (const auto& [objectStreamNumber, object_body] : objectStreams) {
        const QByteArray dictionary = dictionary_part(object_body);
        if (!has_name(dictionary, QStringLiteral("FlateDecode"))) {
            continue;
        }

        const std::optional<int> count = integer_value(dictionary, QStringLiteral("N"));
        const std::optional<int> first = integer_value(dictionary, QStringLiteral("First"));
        if (!count || !first) {
            continue;
        }

        const int streamIndex = object_body.indexOf("stream");
        const int endStreamIndex = object_body.lastIndexOf("endstream");
        if (streamIndex < 0 || endStreamIndex <= streamIndex) {
            continue;
        }

        const int dataStart = stream_data_start(object_body, streamIndex);
        const QByteArray decoded = inflate_flate_data(object_body.mid(dataStart, endStreamIndex - dataStart));
        if (decoded.isEmpty() || decoded.size() <= *first) {
            qCWarning(logMedia) << "Could not decode object stream" << objectStreamNumber;
            continue;
        }
        if (decoded.size() > kMaximumTotalInflatedObjectStreamBytes - total_inflated_bytes) {
            qCWarning(logMedia) << "Stopping object-stream expansion at the aggregate memory limit";
            break;
        }
        total_inflated_bytes += decoded.size();

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
            const int object_number = pairs.at(i).first;
            const int objectStart = *first + pairs.at(i).second;
            const int objectEnd = (i + 1 < pairs.size()) ? *first + pairs.at(i + 1).second : decoded.size();
            if (objectStart >= *first && objectEnd > objectStart && objectEnd <= decoded.size()) {
                objects.insert(object_number, decoded.mid(objectStart, objectEnd - objectStart).trimmed());
            }
        }
    }
}

/** @brief Traverses a PDF page tree and returns page objects in display order. */
QVector<int> page_order_from_node(const ObjectMap& objects, int object_number, std::unordered_set<int>& visited) {
    if (visited.contains(object_number)) {
        return {};
    }
    visited.insert(object_number);

    const QByteArray body = objects.value(object_number);
    if (body.isEmpty()) {
        return {};
    }

    if (has_type(body, QStringLiteral("Page"))) {
        return {object_number};
    }

    QVector<int> pages;
    if (has_type(body, QStringLiteral("Pages"))) {
        const QString kids = bracketed_value(body, QStringLiteral("Kids"));
        for (int child : referenced_objects(kids)) {
            pages += page_order_from_node(objects, child, visited);
        }
    }
    return pages;
}

/** @brief Returns page object numbers in presentation order. */
QVector<int> ordered_page_objects(const ObjectMap& objects) {
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!has_type(it.value(), QStringLiteral("Catalog"))) {
            continue;
        }

        const std::optional<int> pagesRoot = referenced_object(it.value(), QStringLiteral("Pages"));
        if (!pagesRoot) {
            continue;
        }

        std::unordered_set<int> visited;
        QVector<int> pages = page_order_from_node(objects, *pagesRoot, visited);
        if (!pages.isEmpty()) {
            return pages;
        }
    }

    QVector<int> fallback;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (has_type(it.value(), QStringLiteral("Page"))) {
            fallback.push_back(it.key());
        }
    }
    std::sort(fallback.begin(), fallback.end());
    return fallback;
}

/** @brief Returns annotation object numbers referenced by a page. */
QVector<int> annotation_objects_for_page(const ObjectMap& objects, const QByteArray& page_body) {
    if (const std::optional<int> annotsRef = referenced_object(page_body, QStringLiteral("Annots"))) {
        return referenced_objects(QString::fromLatin1(objects.value(*annotsRef)));
    }

    const QString directAnnots = bracketed_value(page_body, QStringLiteral("Annots"));
    return referenced_objects(directAnnots);
}

/** @brief Builds a media annotation from a parsed PDF object. */
PdfMediaAnnotation annotation_from_object(int page_index, int object_number, const QByteArray& body) {
    return PdfMediaAnnotation{
        page_index,
        object_number,
        subtype_for_annotation(body),
        media_file_name(body),
        QString(),
        rect_for_annotation(body),
        QImage()
    };
}

/** @brief Builds a molecule annotation from a parsed PDF object. */
PdfMoleculeAnnotation molecule_annotation_from_object(
    int page_index,
    int object_number,
    const QByteArray& body) {
    return PdfMoleculeAnnotation{
        page_index,
        object_number,
        media_file_name(body),
        QString(),
        rect_for_annotation(body),
        MoleculeGeometry(),
        QString(),
    };
}

/** @brief Returns the decoded payload of a directly represented PDF stream object. */
QByteArray decoded_stream_payload(const QByteArray& object_body, QString* error_message) {
    const QByteArray dictionary = dictionary_part(object_body);
    const int stream_index = object_body.indexOf("stream");
    const int end_stream_index = object_body.lastIndexOf("endstream");
    if (stream_index < 0 || end_stream_index <= stream_index) {
        if (error_message) {
            *error_message = QStringLiteral("Embedded figure does not reference a valid PDF stream");
        }
        return {};
    }

    const int data_start = stream_data_start(object_body, stream_index);
    QByteArray payload = object_body.mid(data_start, end_stream_index - data_start);
    if (has_name(dictionary, QStringLiteral("FlateDecode"))) {
        payload = inflate_flate_data(payload);
    } else if (dictionary.contains("/Filter")) {
        if (error_message) {
            *error_message = QStringLiteral("Embedded figure uses an unsupported PDF stream filter");
        }
        return {};
    }
    if (payload.isEmpty() || payload.size() > kMaximumEmbeddedFigureBytes) {
        if (error_message) {
            *error_message = QStringLiteral("Embedded figure is empty, invalid, or exceeds 4 MiB");
        }
        return {};
    }
    return payload;
}

/** @brief Builds and decodes an interactive figure referenced by a custom annotation. */
PdfInteractiveFigureAnnotation interactive_figure_annotation_from_object(
    const ObjectMap& objects,
    int page_index,
    int object_number,
    const QByteArray& body) {
    PdfInteractiveFigureAnnotation annotation;
    annotation.page_index = page_index;
    annotation.object_number = object_number;
    annotation.rect = rect_for_annotation(body);

    const std::optional<int> asset_ref = referenced_object(body, QStringLiteral("Asset"));
    if (!asset_ref || !objects.contains(*asset_ref)) {
        annotation.error_message = QStringLiteral("Interactive figure has no embedded file specification");
        return annotation;
    }

    const QByteArray file_spec = objects.value(*asset_ref);
    annotation.file_name = pdf_string_value(file_spec, QStringLiteral("UF"));
    if (annotation.file_name.isEmpty()) {
        annotation.file_name = pdf_string_value(file_spec, QStringLiteral("F"));
    }
    const std::optional<int> stream_ref = referenced_object(file_spec, QStringLiteral("F"));
    if (!stream_ref || !objects.contains(*stream_ref)) {
        annotation.error_message = QStringLiteral("Interactive figure embedded stream is missing");
        return annotation;
    }

    const QByteArray stream_object = objects.value(*stream_ref);
    if (!has_type(dictionary_part(stream_object), QStringLiteral("EmbeddedFile"))) {
        annotation.error_message = QStringLiteral("Interactive figure asset is not an embedded file");
        return annotation;
    }
    const QByteArray payload = decoded_stream_payload(stream_object, &annotation.error_message);
    if (payload.isEmpty()) {
        return annotation;
    }
    parse_interactive_figure(payload, &annotation.definition, &annotation.error_message);
    return annotation;
}

/** @brief Normalizes a package-relative asset path. */
QString normalized_package_path(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(path);
}

/** @brief Returns a canonical path when possible and a normalized absolute path otherwise. */
QString canonical_or_absolute_path(const QString& path) {
    const QFileInfo info(path);
    const QString canonical_path = info.canonicalFilePath();
    return QDir::cleanPath(QDir::fromNativeSeparators(
        canonical_path.isEmpty() ? info.absoluteFilePath() : canonical_path));
}

/** @brief Returns whether @p candidate is contained by @p directory. */
bool is_path_within_directory(const QString& candidate, const QString& directory) {
    const QString canonical_candidate = canonical_or_absolute_path(candidate);
    QString canonical_directory = canonical_or_absolute_path(directory);
    if (canonical_candidate.isEmpty() || canonical_directory.isEmpty()) {
        return false;
    }

    if (!canonical_directory.endsWith(QLatin1Char('/'))) {
        canonical_directory.append(QLatin1Char('/'));
    }
#if defined(Q_OS_WIN)
    return canonical_candidate.startsWith(canonical_directory, Qt::CaseInsensitive);
#else
    return canonical_candidate.startsWith(canonical_directory, Qt::CaseSensitive);
#endif
}

/** @brief Resolves an annotation's media filename against a PDF or UIL package root. */
QString resolve_media_path(
    const QString& pdfPath,
    const QString& fileName,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths) {
    if (fileName.isEmpty()) {
        return {};
    }

    const QString normalizedTarget = normalized_package_path(fileName);
    if (!package_root_path.isEmpty()) {
        for (const QString& assetPath : package_movie_asset_paths) {
            if (normalizedTarget == normalized_package_path(assetPath)) {
                const QString candidate = QFileInfo(QDir(package_root_path), normalizedTarget).absoluteFilePath();
                return is_path_within_directory(candidate, package_root_path) ? candidate : QString();
            }
        }
        return {};
    }

    const QFileInfo pdfInfo(pdfPath);
    const QString candidate = QFileInfo(fileName).isAbsolute()
        ? QFileInfo(fileName).absoluteFilePath()
        : QFileInfo(pdfInfo.absoluteDir(), fileName).absoluteFilePath();
    return is_path_within_directory(candidate, pdfInfo.absolutePath()) ? candidate : QString();
}

/** @brief Resolves media paths and extracts preview frames when supported. */
void resolve_and_extract_media_frames(
    PdfMediaScanResult& result,
    const QString& pdfPath,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths) {
    for (PdfMediaAnnotation& annotation : result.annotations) {
        annotation.resolved_file_path = resolve_media_path(pdfPath, annotation.fileName, package_root_path, package_movie_asset_paths);
        if (annotation.is_mp4() && annotation.resolved_file_path.isEmpty()) {
            performance_log::record_event(QStringLiteral("pdf.media_path_rejected"), {
                {QStringLiteral("file_name"), QFileInfo(annotation.fileName).fileName()},
                {QStringLiteral("page"), annotation.page_index + 1}
            });
            continue;
        }
        if (annotation.is_mp4()) {
            performance_log::ScopedSpan frame_span(QStringLiteral("pdf.media_first_frame"), {
                {QStringLiteral("file_name"), QFileInfo(annotation.resolved_file_path).fileName()},
                {QStringLiteral("page"), annotation.page_index + 1}
            });
            QString error_message;
            annotation.first_frame = extract_first_video_frame(annotation.resolved_file_path, &error_message);
            frame_span.add_field(QStringLiteral("frame_ready"), annotation.has_first_frame());
            if (!error_message.isEmpty()) {
                frame_span.add_field(QStringLiteral("error"), error_message);
                frame_span.set_outcome(QStringLiteral("failed"));
                qCInfo(logMedia) << "Could not extract first MP4 frame from" << annotation.resolved_file_path << error_message;
            } else {
                frame_span.set_outcome(QStringLiteral("decoded"));
            }
        }
    }
}

/** @brief Resolves and parses molecule assets referenced by PDF annotations. */
void resolve_and_load_molecules(
    PdfMediaScanResult& result,
    const QString& pdf_path,
    const QString& package_root_path,
    const QStringList& package_molecule_asset_paths) {
    for (PdfMoleculeAnnotation& annotation : result.molecule_annotations) {
        annotation.resolved_file_path = resolve_media_path(
            pdf_path,
            annotation.file_name,
            package_root_path,
            package_molecule_asset_paths);
        if (annotation.resolved_file_path.isEmpty()) {
            annotation.error_message =
                QStringLiteral("Molecule asset path was rejected or is missing");
            continue;
        }
        if (!annotation.resolved_file_path.endsWith(QStringLiteral(".xyz"), Qt::CaseInsensitive)) {
            annotation.error_message = QStringLiteral("Only XYZ molecule assets are supported");
            continue;
        }
        load_xyz_molecule(
            annotation.resolved_file_path,
            &annotation.geometry,
            &annotation.error_message);
    }
}
}

bool PdfMediaAnnotation::has_first_frame() const {
    return !first_frame.isNull();
}

bool PdfMediaAnnotation::is_mp4() const {
    return resolved_file_path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive);
}

bool PdfMoleculeAnnotation::is_ready() const {
    return rect.isValid() && geometry.is_valid() && error_message.isEmpty();
}

bool PdfInteractiveFigureAnnotation::is_ready() const {
    return error_message.isEmpty() && definition.is_valid() && rect.isValid();
}

bool PdfMediaScanResult::has_media() const {
    return !annotations.isEmpty() || !molecule_annotations.isEmpty()
        || !interactive_figure_annotations.isEmpty();
}

QString PdfMediaScanResult::summary() const {
    if (annotations.isEmpty() && molecule_annotations.isEmpty()
        && interactive_figure_annotations.isEmpty()) {
        return QStringLiteral("No PDF media annotations detected");
    }

    QStringList parts;
    for (const PdfMediaAnnotation& annotation : annotations) {
        QString part = annotation.page_index >= 0
            ? QStringLiteral("page %1").arg(annotation.page_index + 1)
            : QStringLiteral("unknown page");
        part += QStringLiteral(" %1").arg(annotation.subtype);
        if (!annotation.fileName.isEmpty()) {
            part += QStringLiteral(" (%1)").arg(annotation.fileName);
        }
        if (annotation.has_first_frame()) {
            part += QStringLiteral(" [first frame ready]");
        }
        parts.push_back(part);
    }
    for (const PdfMoleculeAnnotation& annotation : molecule_annotations) {
        QString part = annotation.page_index >= 0
            ? QStringLiteral("page %1 molecule").arg(annotation.page_index + 1)
            : QStringLiteral("unknown page molecule");
        if (!annotation.file_name.isEmpty()) {
            part += QStringLiteral(" (%1)").arg(annotation.file_name);
        }
        part += annotation.is_ready() ? QStringLiteral(" [geometry ready]")
                                      : QStringLiteral(" [unavailable]");
        parts.push_back(part);
    }

    for (const PdfInteractiveFigureAnnotation& annotation : interactive_figure_annotations) {
        QString part = annotation.page_index >= 0
            ? QStringLiteral("page %1 interactive figure").arg(annotation.page_index + 1)
            : QStringLiteral("unknown page interactive figure");
        if (!annotation.file_name.isEmpty()) {
            part += QStringLiteral(" (%1)").arg(annotation.file_name);
        }
        part += annotation.is_ready() ? QStringLiteral(" [embedded figure ready]")
                                      : QStringLiteral(" [figure unavailable]");
        parts.push_back(part);
    }
    return QStringLiteral("%1 interactive annotation(s): %2")
        .arg(annotations.size() + molecule_annotations.size()
             + interactive_figure_annotations.size())
        .arg(parts.join(QStringLiteral("; ")));
}

PdfMediaScanResult scan_pdf_media_annotations(
    const QString& path,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths,
    const QStringList& package_molecule_asset_paths) {
    PdfMediaScanResult result;
    const QFileInfo file_info(path);
    performance_log::ScopedSpan scan_span(QStringLiteral("pdf.media_scan"), {
        {QStringLiteral("file_name"), file_info.fileName()},
        {QStringLiteral("file_size_bytes"), file_info.size()}
    });

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        scan_span.add_field(QStringLiteral("error_stage"), QStringLiteral("open_file"));
        scan_span.set_outcome(QStringLiteral("failed"));
        qCWarning(logMedia) << "Could not scan PDF media annotations; failed to open" << path;
        return result;
    }
    scan_span.checkpoint(QStringLiteral("open_file"));

    if (file.size() < 0 || file.size() > kMaximumPdfScanBytes) {
        scan_span.add_field(QStringLiteral("error_stage"), QStringLiteral("file_size_limit"));
        scan_span.set_outcome(QStringLiteral("skipped_too_large"));
        qCWarning(logMedia) << "Skipping PDF media scan because the file exceeds the scan limit" << path;
        return result;
    }

    const QByteArray file_bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || file_bytes.size() != file.size()) {
        scan_span.add_field(QStringLiteral("error_stage"), QStringLiteral("read_file"));
        scan_span.set_outcome(QStringLiteral("failed"));
        qCWarning(logMedia) << "Could not completely read PDF for media scanning" << path;
        return result;
    }
    scan_span.checkpoint(
        QStringLiteral("read_file"),
        {{QStringLiteral("bytes_read"), file_bytes.size()}});

    const bool raw_media_candidate = contains_media_marker(file_bytes);
    const bool has_compressed_objects = file_bytes.contains("/ObjStm");
    scan_span.checkpoint(QStringLiteral("raw_media_marker_preflight"), {
        {QStringLiteral("candidate_found"), raw_media_candidate},
        {QStringLiteral("compressed_objects"), has_compressed_objects}
    });
    if (!raw_media_candidate && !has_compressed_objects) {
        scan_span.add_field(QStringLiteral("annotation_count"), 0);
        scan_span.set_outcome(QStringLiteral("no_media_markers"));
        qCInfo(logMedia) << result.summary();
        return result;
    }

    ObjectMap objects = extract_indirect_objects(file_bytes);
    scan_span.checkpoint(
        QStringLiteral("extract_indirect_objects"),
        {{QStringLiteral("object_count"), objects.size()}});
    extract_object_streams(objects);
    scan_span.checkpoint(
        QStringLiteral("extract_object_streams"),
        {{QStringLiteral("object_count"), objects.size()}});

    const bool has_media_candidate = std::any_of(
        objects.cbegin(),
        objects.cend(),
        [](const QByteArray& object_body) {
            return contains_media_marker(object_body);
        });
    scan_span.checkpoint(
        QStringLiteral("media_marker_preflight"),
        {{QStringLiteral("candidate_found"), has_media_candidate}});
    if (!has_media_candidate) {
        scan_span.add_field(QStringLiteral("annotation_count"), 0);
        scan_span.set_outcome(QStringLiteral("no_media_markers"));
        qCInfo(logMedia) << result.summary();
        return result;
    }

    std::unordered_set<int> seen_interactive_objects;
    const QVector<int> pages = ordered_page_objects(objects);
    for (int page_index = 0; page_index < pages.size(); ++page_index) {
        const QByteArray page_body = objects.value(pages.at(page_index));
        for (int annotation_object : annotation_objects_for_page(objects, page_body)) {
            const QByteArray annotation_body = objects.value(annotation_object);
            if (!annotation_body.isEmpty() && is_interactive_figure_annotation(annotation_body)) {
                result.interactive_figure_annotations.push_back(
                    interactive_figure_annotation_from_object(
                        objects, page_index, annotation_object, annotation_body));
                seen_interactive_objects.insert(annotation_object);
            } else if (!annotation_body.isEmpty() && is_molecule_annotation(annotation_body)) {
                result.molecule_annotations.push_back(
                    molecule_annotation_from_object(
                        page_index,
                        annotation_object,
                        annotation_body));
                seen_interactive_objects.insert(annotation_object);
            } else if (!annotation_body.isEmpty() && is_media_annotation(annotation_body)) {
                result.annotations.push_back(
                    annotation_from_object(page_index, annotation_object, annotation_body));
                seen_interactive_objects.insert(annotation_object);
            }
        }
    }
    scan_span.checkpoint(QStringLiteral("scan_page_annotations"), {
        {QStringLiteral("annotation_count"), result.annotations.size()},
        {QStringLiteral("molecule_annotation_count"), result.molecule_annotations.size()},
        {QStringLiteral("interactive_figure_annotation_count"), result.interactive_figure_annotations.size()},
        {QStringLiteral("page_count"), pages.size()}
    });

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (seen_interactive_objects.contains(it.key())) {
            continue;
        }
        if (is_interactive_figure_annotation(it.value())) {
            result.interactive_figure_annotations.push_back(
                interactive_figure_annotation_from_object(objects, -1, it.key(), it.value()));
        } else if (is_molecule_annotation(it.value())) {
            result.molecule_annotations.push_back(
                molecule_annotation_from_object(-1, it.key(), it.value()));
        } else if (is_media_annotation(it.value())) {
            result.annotations.push_back(annotation_from_object(-1, it.key(), it.value()));
        }
    }
    scan_span.checkpoint(
        QStringLiteral("scan_unattached_annotations"),
        {
            {QStringLiteral("annotation_count"), result.annotations.size()},
            {QStringLiteral("molecule_annotation_count"), result.molecule_annotations.size()},
            {QStringLiteral("interactive_figure_annotation_count"), result.interactive_figure_annotations.size()},
        });

    resolve_and_extract_media_frames(result, path, package_root_path, package_movie_asset_paths);
    resolve_and_load_molecules(
        result,
        path,
        package_root_path,
        package_molecule_asset_paths);
    int frames_ready = 0;
    for (const PdfMediaAnnotation& annotation : std::as_const(result.annotations)) {
        if (annotation.has_first_frame()) {
            ++frames_ready;
        }
    }
    scan_span.checkpoint(QStringLiteral("resolve_media_and_extract_frames"), {
        {QStringLiteral("annotation_count"), result.annotations.size()},
        {QStringLiteral("molecule_annotation_count"), result.molecule_annotations.size()},
        {QStringLiteral("interactive_figure_annotation_count"), result.interactive_figure_annotations.size()},
        {QStringLiteral("frames_ready"), frames_ready}
    });
    scan_span.add_field(
        QStringLiteral("annotation_count"),
        result.annotations.size() + result.molecule_annotations.size()
            + result.interactive_figure_annotations.size());
    scan_span.add_field(QStringLiteral("page_count"), pages.size());
    scan_span.set_outcome(QStringLiteral("scanned"));
    qCInfo(logMedia) << result.summary();
    return result;
}
