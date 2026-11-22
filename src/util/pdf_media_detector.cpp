#include "pdf_media_detector.hpp"

#include "media/video_frame_extractor.hpp"
#include "util/performance_log.hpp"

#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <exception>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

Q_LOGGING_CATEGORY(log_media, "pdf.media")

namespace {
using ObjectMap = QHash<int, QByteArray>;
constexpr qint64 kMaximumPdfScanBytes = 512LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumIndirectObjectBytes = 64 * 1024 * 1024;
constexpr qsizetype kMaximumIndirectObjects = 2 * 1024 * 1024;
constexpr qsizetype kMaximumInflatedObjectStreamBytes = 64 * 1024 * 1024;
constexpr qsizetype kMaximumTotalInflatedObjectStreamBytes = 256 * 1024 * 1024;
constexpr qsizetype kMaximumEmbeddedFigureBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumTotalEmbeddedPayloadBytes = 64 * 1024 * 1024;
constexpr int kMaximumPageTreeDepth = 1024;
constexpr qsizetype kMaximumAnnotationReferences = 1024 * 1024;
constexpr qsizetype kMaximumDetectedAnnotations = 4096;

/** @brief Returns whether a byte is ASCII whitespace as matched by a regular-expression `\s`. */
bool is_space_byte(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' || ch == '\r';
}

/** @brief Returns whether a Latin-1 byte is whitespace according to QChar::isSpace(). */
bool is_latin1_space_byte(char ch) {
    return is_space_byte(ch) || uchar(ch) == 0x85 || uchar(ch) == 0xa0;
}

/** @brief Returns whether a byte is an ASCII decimal digit. */
bool is_digit_byte(char ch) {
    return ch >= '0' && ch <= '9';
}

/** @brief Returns whether a byte is an ASCII letter or digit. */
bool is_alphanumeric_byte(char ch) {
    return is_digit_byte(ch) || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

/** @brief Returns whether a byte is a regular-expression word character (`[A-Za-z0-9_]`). */
bool is_word_byte(char ch) {
    return is_alphanumeric_byte(ch) || ch == '_';
}

/** @brief Returns the index of the first non-whitespace byte at or after @p index. */
qsizetype skip_spaces(QByteArrayView bytes, qsizetype index) {
    while (index < bytes.size() && is_space_byte(bytes.at(index))) {
        ++index;
    }
    return index;
}

/** @brief Returns the index just past the run of ASCII digits that starts at @p index. */
qsizetype skip_digits(QByteArrayView bytes, qsizetype index) {
    while (index < bytes.size() && is_digit_byte(bytes.at(index))) {
        ++index;
    }
    return index;
}

/** @brief Returns the start of the whitespace run that ends just before @p index. */
qsizetype skip_spaces_backward(QByteArrayView bytes, qsizetype index) {
    while (index > 0 && is_space_byte(bytes.at(index - 1))) {
        --index;
    }
    return index;
}

/** @brief Returns the start of the digit run that ends just before @p index. */
qsizetype skip_digits_backward(QByteArrayView bytes, qsizetype index) {
    while (index > 0 && is_digit_byte(bytes.at(index - 1))) {
        --index;
    }
    return index;
}

/**
 * @brief Converts a run of ASCII digits to an int.
 * @return The value, or 0 when it does not fit in an int (matching QString::toInt() failure).
 */
int parse_decimal(QByteArrayView bytes, qsizetype begin, qsizetype end) {
    qint64 value = 0;
    for (qsizetype index = begin; index < end; ++index) {
        value = value * 10 + (bytes.at(index) - '0');
        if (value > INT_MAX) {
            return 0;
        }
    }
    return int(value);
}

/**
 * @brief Finds the first `/name` token followed by a word boundary, like the regex `/name\b`.
 * @return The index just past the name, or -1 when the token does not occur.
 */
qsizetype find_name_token(const QByteArray& bytes, QByteArrayView name, qsizetype from = 0) {
    const QByteArray needle = QByteArray(1, '/') + name.toByteArray();
    qsizetype index = from;
    while ((index = bytes.indexOf(needle, index)) >= 0) {
        const qsizetype end = index + needle.size();
        if (end == bytes.size() || !is_word_byte(bytes.at(end))) {
            return end;
        }
        ++index;
    }
    return -1;
}

/**
 * @brief Parses an indirect reference `N G R` whose object number starts at @p index.
 * @param object_number Receives the referenced object number on success.
 * @param end Receives the index just past the `R` keyword on success.
 * @return Whether a complete reference was found.
 */
bool parse_reference_at(QByteArrayView bytes, qsizetype index, int* object_number, qsizetype* end) {
    const qsizetype number_end = skip_digits(bytes, index);
    const qsizetype generation_begin = skip_spaces(bytes, number_end);
    const qsizetype generation_end = skip_digits(bytes, generation_begin);
    const qsizetype keyword = skip_spaces(bytes, generation_end);
    if (number_end == index || generation_begin == number_end
        || generation_end == generation_begin || keyword == generation_end
        || keyword >= bytes.size() || bytes.at(keyword) != 'R') {
        return false;
    }
    *object_number = parse_decimal(bytes, index, number_end);
    *end = keyword + 1;
    return true;
}

/**
 * @brief Inflates PDF stream data encoded with the Flate filter.
 * @param input Compressed bytes; must be smaller than 4 GiB.
 * @param maximum_output_bytes Decompression stops and fails once the output exceeds this size.
 * @param produced_bytes When provided, receives the number of bytes inflated, even on failure,
 * so that callers can charge aborted decompression against an aggregate budget.
 * @return The inflated data, or an empty array for corrupt, truncated, or oversized streams.
 */
QByteArray inflate_flate_data(
    QByteArrayView input,
    qsizetype maximum_output_bytes,
    qsizetype* produced_bytes = nullptr) {
    if (produced_bytes) {
        *produced_bytes = 0;
    }
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
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
        const qsizetype produced = qsizetype(buffer.size() - stream.avail_out);
        if (produced_bytes) {
            *produced_bytes += produced;
        }
        if (output.size() + produced > maximum_output_bytes) {
            inflateEnd(&stream);
            return {};
        }
        output.append(buffer.data(), produced);
    }

    inflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray{};
}

/** @brief Returns the first payload byte following a PDF stream keyword. */
qsizetype stream_data_start(const QByteArray& object_body, qsizetype stream_keyword_index) {
    const qsizetype start = stream_keyword_index + qsizetype(sizeof("stream") - 1);
    if (object_body.mid(start, 2) == "\r\n") {
        return start + 2;
    }
    if (start < object_body.size()
        && (object_body.at(start) == '\n' || object_body.at(start) == '\r')) {
        return start + 1;
    }
    return start;
}

/** @brief Returns the dictionary portion of a PDF indirect object. */
QByteArray dictionary_part(const QByteArray& object_body) {
    const qsizetype stream_index = object_body.indexOf("stream");
    return stream_index >= 0 ? object_body.left(stream_index) : object_body;
}

/** @brief Reads the integer following a PDF dictionary key, like the regex `/key\s+(\d+)`. */
std::optional<int> integer_value(const QByteArray& bytes, QByteArrayView key) {
    const QByteArray needle = QByteArray(1, '/') + key.toByteArray();
    qsizetype index = 0;
    while ((index = bytes.indexOf(needle, index)) >= 0) {
        const qsizetype key_end = index + needle.size();
        const qsizetype digits_begin = skip_spaces(bytes, key_end);
        const qsizetype digits_end = skip_digits(bytes, digits_begin);
        if (digits_begin > key_end && digits_end > digits_begin) {
            return parse_decimal(bytes, digits_begin, digits_end);
        }
        ++index;
    }
    return std::nullopt;
}

/** @brief Returns whether PDF bytes contain a particular name object (`/name\b`). */
bool has_name(const QByteArray& bytes, QByteArrayView name) {
    return find_name_token(bytes, name) >= 0;
}

/** @brief Returns whether a PDF dictionary declares a particular type (`/Type\s*\/name\b`). */
bool has_type(const QByteArray& bytes, QByteArrayView type_name) {
    qsizetype index = 0;
    while ((index = bytes.indexOf("/Type", index)) >= 0) {
        const qsizetype value = skip_spaces(bytes, index + 5);
        if (value < bytes.size() && bytes.at(value) == '/'
            && QByteArrayView(bytes).sliced(value + 1).startsWith(type_name)) {
            const qsizetype end = value + 1 + type_name.size();
            if (end == bytes.size() || !is_word_byte(bytes.at(end))) {
                return true;
            }
        }
        ++index;
    }
    return false;
}

/** @brief Reads the object number of an indirect reference associated with a dictionary key. */
std::optional<int> referenced_object(const QByteArray& bytes, QByteArrayView key) {
    const QByteArray needle = QByteArray(1, '/') + key.toByteArray();
    qsizetype index = 0;
    while ((index = bytes.indexOf(needle, index)) >= 0) {
        const qsizetype key_end = index + needle.size();
        const qsizetype number_begin = skip_spaces(bytes, key_end);
        int object_number = 0;
        qsizetype reference_end = 0;
        if (number_begin > key_end
            && parse_reference_at(bytes, number_begin, &object_number, &reference_end)) {
            return object_number;
        }
        ++index;
    }
    return std::nullopt;
}

/**
 * @brief Extracts all indirect-object references (`N G R`) from PDF source bytes, in order.
 * Runs in time linear in the input, including long runs of digits or whitespace.
 */
QVector<int> referenced_objects(const QByteArray& bytes) {
    QVector<int> refs;
    qsizetype index = 0;
    while (index < bytes.size()) {
        if (!is_digit_byte(bytes.at(index))) {
            ++index;
            continue;
        }
        int object_number = 0;
        qsizetype reference_end = 0;
        if (parse_reference_at(bytes, index, &object_number, &reference_end)) {
            refs.push_back(object_number);
            index = reference_end;
        } else {
            index = skip_digits(bytes, index);
        }
    }
    return refs;
}

/** @brief Reads the contents of the first `[...]` array following a dictionary key. */
QByteArray bracketed_value(const QByteArray& bytes, QByteArrayView key) {
    const qsizetype key_index = bytes.indexOf(QByteArray(1, '/') + key.toByteArray());
    if (key_index < 0) {
        return {};
    }

    const qsizetype begin = bytes.indexOf('[', key_index);
    if (begin < 0) {
        return {};
    }
    const qsizetype end = bytes.indexOf(']', begin + 1);
    if (end < 0) {
        return {};
    }
    return bytes.mid(begin + 1, end - begin - 1);
}

/** @brief Decodes a PDF byte string, including UTF-16 byte-order marks. */
QString decoded_pdf_string_bytes(const QByteArray& decoded) {
    if (decoded.size() >= 2
        && ((uchar(decoded.at(0)) == 0xfe && uchar(decoded.at(1)) == 0xff)
            || (uchar(decoded.at(0)) == 0xff && uchar(decoded.at(1)) == 0xfe))) {
        const bool big_endian = uchar(decoded.at(0)) == 0xfe;
        QString result;
        result.reserve((decoded.size() - 2) / 2);
        for (qsizetype i = 2; i + 1 < decoded.size(); i += 2) {
            const ushort first = uchar(decoded.at(i));
            const ushort second = uchar(decoded.at(i + 1));
            result.append(QChar(big_endian ? ushort((first << 8) | second)
                                           : ushort(first | (second << 8))));
        }
        return result;
    }
    return QString::fromLatin1(decoded);
}

/**
 * @brief Reads and decodes the PDF string that follows the first `/key` token.
 * Literal `(...)` and hexadecimal `<...>` strings are supported; any other value yields an
 * empty string.
 */
QString pdf_string_value(const QByteArray& bytes, QByteArrayView key) {
    qsizetype begin = find_name_token(bytes, key);
    if (begin < 0) {
        return {};
    }

    while (begin < bytes.size() && is_latin1_space_byte(bytes.at(begin))) {
        ++begin;
    }
    if (begin >= bytes.size()) {
        return {};
    }

    if (bytes.at(begin) == '<' && begin + 1 < bytes.size() && bytes.at(begin + 1) != '<') {
        const qsizetype end = bytes.indexOf('>', begin + 1);
        if (end < 0) {
            return {};
        }
        QByteArray hex = bytes.mid(begin + 1, end - begin - 1);
        hex.removeIf([](char ch) { return std::isspace(static_cast<unsigned char>(ch)); });
        return decoded_pdf_string_bytes(QByteArray::fromHex(hex));
    }

    if (bytes.at(begin) != '(') {
        return {};
    }

    QByteArray decoded;
    int depth = 1;
    for (qsizetype i = begin + 1; i < bytes.size(); ++i) {
        const char ch = bytes.at(i);
        if (ch == '\\') {
            if (++i >= bytes.size()) {
                break;
            }
            const char escaped = bytes.at(i);
            if (escaped >= '0' && escaped <= '7') {
                int value = escaped - '0';
                int digits = 1;
                while (digits < 3 && i + 1 < bytes.size()
                       && bytes.at(i + 1) >= '0'
                       && bytes.at(i + 1) <= '7') {
                    value = value * 8 + bytes.at(++i) - '0';
                    ++digits;
                }
                decoded.append(char(value & 0xff));
            } else if (escaped == 'n') {
                decoded.append('\n');
            } else if (escaped == 'r') {
                decoded.append('\r');
            } else if (escaped == 't') {
                decoded.append('\t');
            } else if (escaped == 'b') {
                decoded.append('\b');
            } else if (escaped == 'f') {
                decoded.append('\f');
            } else if (escaped == '\r' || escaped == '\n') {
                if (escaped == '\r' && i + 1 < bytes.size() && bytes.at(i + 1) == '\n') {
                    ++i;
                }
            } else {
                decoded.append(escaped);
            }
            continue;
        }
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        decoded.append(ch);
    }
    return decoded_pdf_string_bytes(decoded);
}

/**
 * @brief Returns the subtype declared by a PDF annotation dictionary.
 * @return The first `/Subtype /Name` value, or `Media` when none is declared.
 */
QString subtype_for_annotation(const QByteArray& bytes) {
    qsizetype index = 0;
    while ((index = bytes.indexOf("/Subtype", index)) >= 0) {
        const qsizetype slash = skip_spaces(bytes, index + 8);
        if (slash < bytes.size() && bytes.at(slash) == '/') {
            qsizetype end = slash + 1;
            while (end < bytes.size() && is_alphanumeric_byte(bytes.at(end))) {
                ++end;
            }
            if (end > slash + 1 && (end == bytes.size() || !is_word_byte(bytes.at(end)))) {
                return QString::fromLatin1(bytes.mid(slash + 1, end - slash - 1));
            }
        }
        ++index;
    }
    return QStringLiteral("Media");
}

/** @brief Reads an annotation rectangle from a PDF dictionary. */
QRectF rect_for_annotation(const QByteArray& bytes) {
    const QString rect_text = QString::fromLatin1(bracketed_value(bytes, "Rect"));
    if (rect_text.isEmpty()) {
        return {};
    }

    const QStringList parts = rect_text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
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
    for (const char* key : {"UF", "F", "DOS", "Mac", "Unix"}) {
        const QString value = pdf_string_value(bytes, key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

/** @brief Returns whether a PDF annotation can contain playable media. */
bool is_media_annotation(const QByteArray& bytes) {
    if (has_type(dictionary_part(bytes), "EmbeddedFile")) {
        return false;
    }
    return has_name(bytes, "Movie")
        || has_name(bytes, "RichMedia")
        || has_name(bytes, "Rendition")
        || has_name(bytes, "EmbeddedFile")
        || has_name(bytes, "Sound")
        || bytes.contains("/Subtype/Movie")
        || bytes.contains("/Subtype /Movie")
        || bytes.contains("/Subtype/RichMedia")
        || bytes.contains("/Subtype /RichMedia");
}

/** @brief Returns whether a PDF annotation describes a UIL molecule. */
bool is_molecule_annotation(const QByteArray& bytes) {
    return has_name(bytes, "UILMolecule");
}

/** @brief Returns whether a PDF annotation describes an embedded UIL interactive figure. */
bool is_interactive_figure_annotation(const QByteArray& bytes) {
    return has_name(bytes, "UILInteractiveFigure");
}

/** @brief Returns whether a PDF annotation describes an embedded UIL atomic orbital. */
bool is_atomic_orbital_annotation(const QByteArray& bytes) {
    return has_name(bytes, "UILAtomicOrbital");
}

/** @brief Returns whether a PDF annotation describes embedded molecular symmetry. */
bool is_molecular_symmetry_annotation(const QByteArray& bytes) {
    return has_name(bytes, "UILMolecularSymmetry");
}

/** @brief Cheaply detects whether object bytes could contain a supported media annotation. */
bool contains_media_marker(const QByteArray& bytes) {
    return bytes.contains("/Movie")
        || bytes.contains("/RichMedia")
        || bytes.contains("/Rendition")
        || bytes.contains("/EmbeddedFile")
        || bytes.contains("/Sound")
        || bytes.contains("/UILMolecule")
        || bytes.contains("/UILInteractiveFigure")
        || bytes.contains("/UILAtomicOrbital")
        || bytes.contains("/UILMolecularSymmetry");
}

/** @brief Location of one `N G obj` indirect-object header in the PDF bytes. */
struct ObjectHeader {
    qsizetype begin = -1;
    qsizetype body_begin = -1;
    int object_number = 0;
};

/**
 * @brief Finds the next `N G obj` header (regex `(\d+)\s+\d+\s+obj\b`) at or after @p from.
 * Successive calls with increasing @p from scan each byte a bounded number of times, because
 * the backward walk from an `obj` keyword never crosses an earlier keyword.
 */
std::optional<ObjectHeader> next_object_header(const QByteArray& pdf_bytes, qsizetype from) {
    qsizetype keyword = from;
    while ((keyword = pdf_bytes.indexOf("obj", keyword)) >= 0) {
        const qsizetype keyword_end = keyword + 3;
        if (keyword_end < pdf_bytes.size() && is_word_byte(pdf_bytes.at(keyword_end))) {
            keyword = keyword_end;
            continue;
        }
        const qsizetype generation_end = std::max(skip_spaces_backward(pdf_bytes, keyword), from);
        const qsizetype generation_begin =
            std::max(skip_digits_backward(pdf_bytes, generation_end), from);
        const qsizetype number_end =
            std::max(skip_spaces_backward(pdf_bytes, generation_begin), from);
        const qsizetype number_begin = std::max(skip_digits_backward(pdf_bytes, number_end), from);
        if (generation_end < keyword && generation_begin < generation_end
            && number_end < generation_begin && number_begin < number_end) {
            return ObjectHeader{
                number_begin, keyword_end, parse_decimal(pdf_bytes, number_begin, number_end)};
        }
        keyword = keyword_end;
    }
    return std::nullopt;
}

/**
 * @brief Extracts directly represented indirect objects from PDF bytes.
 *
 * Each body ends at the earlier of its `endobj` keyword and the next object header, so malformed
 * files whose headers lack `endobj` cannot make bodies overlap. Bodies are zero-copy views into
 * @p pdf_bytes, which must outlive the returned map; each body is truncated to
 * kMaximumIndirectObjectBytes and at most kMaximumIndirectObjects distinct objects are kept.
 * Runs in time linear in the file size. Later definitions of an object number replace earlier
 * ones, as with incremental updates.
 */
ObjectMap extract_indirect_objects(const QByteArray& pdf_bytes) {
    ObjectMap objects;
    qsizetype next_endobj = -1;
    bool endobj_exhausted = false;
    std::optional<ObjectHeader> header = next_object_header(pdf_bytes, 0);
    while (header) {
        const std::optional<ObjectHeader> next_header =
            next_object_header(pdf_bytes, header->body_begin);
        if (!endobj_exhausted && next_endobj < header->body_begin) {
            next_endobj = pdf_bytes.indexOf("endobj", header->body_begin);
            endobj_exhausted = next_endobj < 0;
        }

        qsizetype body_end = -1;
        if (!endobj_exhausted && (!next_header || next_endobj <= next_header->begin)) {
            body_end = next_endobj;
        } else if (next_header) {
            body_end = next_header->begin;
        }

        if (body_end >= 0) {
            const qsizetype body_begin = skip_spaces(pdf_bytes, header->body_begin);
            body_end = std::max(body_begin, skip_spaces_backward(pdf_bytes, body_end));
            body_end = std::min(body_end, body_begin + kMaximumIndirectObjectBytes);
            if (objects.size() >= kMaximumIndirectObjects
                && !objects.contains(header->object_number)) {
                qCWarning(log_media) << "Stopping PDF object extraction at the object-count limit"
                                    << kMaximumIndirectObjects;
                break;
            }
            objects.insert(
                header->object_number,
                QByteArray::fromRawData(pdf_bytes.constData() + body_begin, body_end - body_begin));
        }
        header = next_header;
    }
    return objects;
}

/**
 * @brief Expands Flate-compressed PDF object streams into the object map.
 * Decompression is limited per stream and in aggregate (including streams that fail to
 * decode), and no objects are added beyond kMaximumIndirectObjects.
 */
void extract_object_streams(ObjectMap& objects) {
    QVector<std::pair<int, QByteArray>> object_streams;
    qsizetype total_inflated_bytes = 0;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (has_type(dictionary_part(it.value()), "ObjStm")) {
            object_streams.push_back({it.key(), it.value()});
        }
    }
    std::sort(object_streams.begin(), object_streams.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    for (const auto& [object_stream_number, object_body] : object_streams) {
        const QByteArray dictionary = dictionary_part(object_body);
        if (!has_name(dictionary, "FlateDecode")) {
            continue;
        }

        const std::optional<int> count = integer_value(dictionary, "N");
        const std::optional<int> first = integer_value(dictionary, "First");
        if (!count || !first) {
            continue;
        }

        const qsizetype stream_index = object_body.indexOf("stream");
        const qsizetype end_stream_index = object_body.lastIndexOf("endstream");
        if (stream_index < 0 || end_stream_index <= stream_index) {
            continue;
        }

        const qsizetype remaining_budget =
            kMaximumTotalInflatedObjectStreamBytes - total_inflated_bytes;
        if (remaining_budget <= 0) {
            qCWarning(log_media) << "Stopping object-stream expansion at the aggregate memory limit";
            break;
        }
        const qsizetype data_start = stream_data_start(object_body, stream_index);
        qsizetype produced_bytes = 0;
        const QByteArray decoded = inflate_flate_data(
            QByteArrayView(object_body).sliced(data_start, end_stream_index - data_start),
            std::min(kMaximumInflatedObjectStreamBytes, remaining_budget),
            &produced_bytes);
        total_inflated_bytes += produced_bytes;
        if (decoded.isEmpty() || decoded.size() <= *first) {
            qCWarning(log_media) << "Could not decode object stream" << object_stream_number;
            continue;
        }

        const QByteArray header = decoded.left(*first);
        QVector<std::pair<int, qsizetype>> pairs;
        qsizetype index = 0;
        while (index < header.size() && pairs.size() < *count) {
            if (!is_digit_byte(header.at(index))) {
                ++index;
                continue;
            }
            const qsizetype number_end = skip_digits(header, index);
            const qsizetype offset_begin = skip_spaces(header, number_end);
            const qsizetype offset_end = skip_digits(header, offset_begin);
            if (offset_begin == number_end || offset_end == offset_begin) {
                index = number_end;
                continue;
            }
            pairs.push_back({parse_decimal(header, index, number_end),
                             parse_decimal(header, offset_begin, offset_end)});
            index = offset_end;
        }

        std::stable_sort(pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second < rhs.second;
        });

        for (qsizetype i = 0; i < pairs.size(); ++i) {
            const int object_number = pairs.at(i).first;
            const qsizetype object_start = *first + pairs.at(i).second;
            const qsizetype object_end =
                (i + 1 < pairs.size()) ? *first + pairs.at(i + 1).second : decoded.size();
            if (object_start >= *first && object_end > object_start
                && object_end <= decoded.size()) {
                if (objects.size() >= kMaximumIndirectObjects && !objects.contains(object_number)) {
                    qCWarning(log_media)
                        << "Stopping object-stream expansion at the object-count limit";
                    return;
                }
                objects.insert(
                    object_number, decoded.mid(object_start, object_end - object_start).trimmed());
            }
        }
    }
}

/**
 * @brief Appends the page objects below a page-tree node to @p pages in display order.
 *
 * Traverses iteratively (pre-order, kids in array order) so that deeply nested trees cannot
 * overflow the stack. Nodes already in @p visited are skipped, which breaks reference cycles,
 * and the kids of /Pages nodes deeper than kMaximumPageTreeDepth are ignored.
 */
void append_page_tree(
    const ObjectMap& objects,
    int root_object_number,
    std::unordered_set<int>& visited,
    QVector<int>& pages) {
    struct PendingNode {
        int object_number = -1;
        int depth = 0;
    };
    std::vector<PendingNode> pending{{root_object_number, 0}};
    bool depth_limit_reported = false;
    while (!pending.empty()) {
        const PendingNode node = pending.back();
        pending.pop_back();
        if (!visited.insert(node.object_number).second) {
            continue;
        }

        const QByteArray body = objects.value(node.object_number);
        if (body.isEmpty()) {
            continue;
        }
        if (has_type(body, "Page")) {
            pages.push_back(node.object_number);
            continue;
        }
        if (!has_type(body, "Pages")) {
            continue;
        }
        if (node.depth >= kMaximumPageTreeDepth) {
            if (!depth_limit_reported) {
                qCWarning(log_media) << "Ignoring PDF page-tree nodes nested deeper than"
                                    << kMaximumPageTreeDepth;
                depth_limit_reported = true;
            }
            continue;
        }

        const QVector<int> kids = referenced_objects(bracketed_value(body, "Kids"));
        for (auto kid = kids.crbegin(); kid != kids.crend(); ++kid) {
            pending.push_back({*kid, node.depth + 1});
        }
    }
}

/**
 * @brief Returns page object numbers in presentation order.
 * Uses the page tree of the first catalog (in object-number order) that yields pages; falls back
 * to all /Page objects in object-number order when no page tree is usable.
 */
QVector<int> ordered_page_objects(const ObjectMap& objects) {
    QVector<int> catalogs;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (has_type(it.value(), "Catalog")) {
            catalogs.push_back(it.key());
        }
    }
    std::sort(catalogs.begin(), catalogs.end());

    std::unordered_set<int> visited;
    for (int catalog : std::as_const(catalogs)) {
        const std::optional<int> pages_root = referenced_object(objects.value(catalog), "Pages");
        if (!pages_root) {
            continue;
        }

        QVector<int> pages;
        append_page_tree(objects, *pages_root, visited, pages);
        if (!pages.isEmpty()) {
            return pages;
        }
    }

    QVector<int> fallback;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (has_type(it.value(), "Page")) {
            fallback.push_back(it.key());
        }
    }
    std::sort(fallback.begin(), fallback.end());
    return fallback;
}

/**
 * @brief Returns annotation object numbers referenced by a page.
 * @param annotation_arrays Cache of parsed indirect /Annots arrays keyed by object number, so a
 * large array shared by many pages is parsed only once.
 */
QVector<int> annotation_objects_for_page(
    const ObjectMap& objects,
    const QByteArray& page_body,
    QHash<int, QVector<int>>& annotation_arrays) {
    if (const std::optional<int> annots_ref = referenced_object(page_body, "Annots")) {
        const auto cached = annotation_arrays.constFind(*annots_ref);
        if (cached != annotation_arrays.cend()) {
            return cached.value();
        }
        return annotation_arrays.insert(*annots_ref, referenced_objects(objects.value(*annots_ref)))
            .value();
    }

    return referenced_objects(bracketed_value(page_body, "Annots"));
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

/**
 * @brief Returns the decoded payload of a directly represented PDF stream object.
 * @param error_message Receives a user-facing description when decoding fails.
 * @param remaining_budget Aggregate decompression budget for the scan; decoded (or aborted)
 * bytes are subtracted and decoding is refused once it is exhausted.
 * @return The payload, or an empty array on failure.
 */
QByteArray decoded_stream_payload(
    const QByteArray& object_body,
    QString* error_message,
    qsizetype* remaining_budget) {
    const QByteArray dictionary = dictionary_part(object_body);
    const qsizetype stream_index = object_body.indexOf("stream");
    const qsizetype end_stream_index = object_body.lastIndexOf("endstream");
    if (stream_index < 0 || end_stream_index <= stream_index) {
        if (error_message) {
            *error_message = QStringLiteral("Embedded figure does not reference a valid PDF stream");
        }
        return {};
    }
    if (*remaining_budget <= 0) {
        if (error_message) {
            *error_message = QStringLiteral(
                "Embedded figure skipped: the PDF's embedded-data limit was reached");
        }
        return {};
    }

    const qsizetype data_start = stream_data_start(object_body, stream_index);
    const QByteArrayView raw_payload =
        QByteArrayView(object_body).sliced(data_start, end_stream_index - data_start);
    QByteArray payload;
    if (has_name(dictionary, "FlateDecode")) {
        qsizetype produced_bytes = 0;
        payload = inflate_flate_data(raw_payload, kMaximumEmbeddedFigureBytes, &produced_bytes);
        *remaining_budget -= produced_bytes;
    } else if (dictionary.contains("/Filter")) {
        if (error_message) {
            *error_message = QStringLiteral("Embedded figure uses an unsupported PDF stream filter");
        }
        return {};
    } else if (raw_payload.size() <= kMaximumEmbeddedFigureBytes) {
        payload = raw_payload.toByteArray();
        *remaining_budget -= payload.size();
    }
    if (payload.isEmpty() || payload.size() > kMaximumEmbeddedFigureBytes) {
        if (error_message) {
            *error_message = QStringLiteral("Embedded figure is empty, invalid, or exceeds 4 MiB");
        }
        return {};
    }
    return payload;
}

/**
 * @brief Builds and decodes an interactive figure referenced by a custom annotation.
 * @param remaining_payload_budget Aggregate embedded-payload budget, see decoded_stream_payload().
 */
PdfInteractiveFigureAnnotation interactive_figure_annotation_from_object(
    const ObjectMap& objects,
    int page_index,
    int object_number,
    const QByteArray& body,
    qsizetype* remaining_payload_budget) {
    PdfInteractiveFigureAnnotation annotation;
    annotation.page_index = page_index;
    annotation.object_number = object_number;
    annotation.rect = rect_for_annotation(body);

    const std::optional<int> asset_ref = referenced_object(body, "Asset");
    if (!asset_ref || !objects.contains(*asset_ref)) {
        annotation.error_message = QStringLiteral("Interactive figure has no embedded file specification");
        return annotation;
    }

    const QByteArray file_spec = objects.value(*asset_ref);
    annotation.file_name = pdf_string_value(file_spec, "UF");
    if (annotation.file_name.isEmpty()) {
        annotation.file_name = pdf_string_value(file_spec, "F");
    }
    const std::optional<int> stream_ref = referenced_object(file_spec, "F");
    if (!stream_ref || !objects.contains(*stream_ref)) {
        annotation.error_message = QStringLiteral("Interactive figure embedded stream is missing");
        return annotation;
    }

    const QByteArray stream_object = objects.value(*stream_ref);
    if (!has_type(dictionary_part(stream_object), "EmbeddedFile")) {
        annotation.error_message = QStringLiteral("Interactive figure asset is not an embedded file");
        return annotation;
    }
    const QByteArray payload = decoded_stream_payload(
        stream_object, &annotation.error_message, remaining_payload_budget);
    if (payload.isEmpty()) {
        return annotation;
    }
    parse_interactive_figure(payload, &annotation.definition, &annotation.error_message);
    return annotation;
}

/**
 * @brief Builds and decodes an atomic orbital referenced by a custom annotation.
 * @param remaining_payload_budget Aggregate embedded-payload budget, see decoded_stream_payload().
 */
PdfAtomicOrbitalAnnotation atomic_orbital_annotation_from_object(
    const ObjectMap& objects,
    int page_index,
    int object_number,
    const QByteArray& body,
    qsizetype* remaining_payload_budget) {
    PdfAtomicOrbitalAnnotation annotation;
    annotation.page_index = page_index;
    annotation.object_number = object_number;
    annotation.rect = rect_for_annotation(body);

    const std::optional<int> asset_ref = referenced_object(body, "Asset");
    if (!asset_ref || !objects.contains(*asset_ref)) {
        annotation.error_message = QStringLiteral("Atomic orbital has no embedded file specification");
        return annotation;
    }
    const QByteArray file_spec = objects.value(*asset_ref);
    annotation.file_name = pdf_string_value(file_spec, "UF");
    if (annotation.file_name.isEmpty()) {
        annotation.file_name = pdf_string_value(file_spec, "F");
    }
    const std::optional<int> stream_ref = referenced_object(file_spec, "F");
    if (!stream_ref || !objects.contains(*stream_ref)) {
        annotation.error_message = QStringLiteral("Atomic-orbital embedded stream is missing");
        return annotation;
    }
    const QByteArray stream_object = objects.value(*stream_ref);
    if (!has_type(dictionary_part(stream_object), "EmbeddedFile")) {
        annotation.error_message = QStringLiteral("Atomic-orbital asset is not an embedded file");
        return annotation;
    }
    const QByteArray payload = decoded_stream_payload(
        stream_object, &annotation.error_message, remaining_payload_budget);
    if (!payload.isEmpty()) {
        parse_atomic_orbital(payload, &annotation.definition, &annotation.error_message);
    }
    return annotation;
}

/**
 * @brief Builds and decodes molecular symmetry referenced by a custom annotation.
 * @param remaining_payload_budget Aggregate embedded-payload budget, see decoded_stream_payload().
 */
PdfMolecularSymmetryAnnotation molecular_symmetry_annotation_from_object(
    const ObjectMap& objects,
    int page_index,
    int object_number,
    const QByteArray& body,
    qsizetype* remaining_payload_budget) {
    PdfMolecularSymmetryAnnotation annotation;
    annotation.page_index = page_index;
    annotation.object_number = object_number;
    annotation.rect = rect_for_annotation(body);
    const std::optional<int> asset_ref = referenced_object(body, "Asset");
    if (!asset_ref || !objects.contains(*asset_ref)) {
        annotation.error_message =
            QStringLiteral("Molecular symmetry has no embedded file specification");
        return annotation;
    }
    const QByteArray file_spec = objects.value(*asset_ref);
    annotation.file_name = pdf_string_value(file_spec, "UF");
    if (annotation.file_name.isEmpty()) {
        annotation.file_name = pdf_string_value(file_spec, "F");
    }
    const std::optional<int> stream_ref = referenced_object(file_spec, "F");
    if (!stream_ref || !objects.contains(*stream_ref)) {
        annotation.error_message = QStringLiteral("Molecular-symmetry embedded stream is missing");
        return annotation;
    }
    const QByteArray stream_object = objects.value(*stream_ref);
    if (!has_type(dictionary_part(stream_object), "EmbeddedFile")) {
        annotation.error_message = QStringLiteral("Molecular-symmetry asset is not an embedded file");
        return annotation;
    }
    const QByteArray payload = decoded_stream_payload(
        stream_object, &annotation.error_message, remaining_payload_budget);
    if (!payload.isEmpty()) {
        parse_molecular_symmetry(payload, &annotation.definition, &annotation.error_message);
    }
    return annotation;
}

/** @brief Kind of UIL-supported annotation an object describes, in detection priority order. */
enum class AnnotationKind {
    kNone,
    kMolecularSymmetry,
    kAtomicOrbital,
    kInteractiveFigure,
    kMolecule,
    kMedia,
};

/** @brief Classifies an object body; earlier kinds take precedence over later ones. */
AnnotationKind classify_annotation(const QByteArray& body) {
    if (body.isEmpty()) {
        return AnnotationKind::kNone;
    }
    if (is_molecular_symmetry_annotation(body)) {
        return AnnotationKind::kMolecularSymmetry;
    }
    if (is_atomic_orbital_annotation(body)) {
        return AnnotationKind::kAtomicOrbital;
    }
    if (is_interactive_figure_annotation(body)) {
        return AnnotationKind::kInteractiveFigure;
    }
    if (is_molecule_annotation(body)) {
        return AnnotationKind::kMolecule;
    }
    if (is_media_annotation(body)) {
        return AnnotationKind::kMedia;
    }
    return AnnotationKind::kNone;
}

/**
 * @brief Collects annotations into a scan result while bounding the total work.
 *
 * Classification and decoding are memoized per object number, so an annotation object referenced
 * from many pages is parsed once and only re-emitted with a different page index. At most
 * kMaximumDetectedAnnotations annotations are collected and embedded payload decoding shares one
 * kMaximumTotalEmbeddedPayloadBytes budget.
 */
class AnnotationCollector final {
public:
    /** @brief Creates a collector reading @p objects and appending to @p result. */
    AnnotationCollector(const ObjectMap& objects, PdfMediaScanResult& result)
        : objects_(objects), result_(result) {}

    /**
     * @brief Classifies an object and appends the matching annotation for @p page_index.
     * @return The detected kind (kNone for unsupported objects), even when the annotation was
     * dropped because the collection limit was reached.
     */
    AnnotationKind add(int page_index, int object_number, const QByteArray& body) {
        const AnnotationKind kind = kind_of(object_number, body);
        if (kind == AnnotationKind::kNone || is_full()) {
            return kind;
        }

        switch (kind) {
        case AnnotationKind::kMolecularSymmetry:
            result_.molecular_symmetry_annotations.push_back(cached(
                molecular_symmetry_cache_, page_index, object_number, [&] {
                    return molecular_symmetry_annotation_from_object(
                        objects_, page_index, object_number, body, &remaining_payload_bytes_);
                }));
            break;
        case AnnotationKind::kAtomicOrbital:
            result_.atomic_orbital_annotations.push_back(cached(
                atomic_orbital_cache_, page_index, object_number, [&] {
                    return atomic_orbital_annotation_from_object(
                        objects_, page_index, object_number, body, &remaining_payload_bytes_);
                }));
            break;
        case AnnotationKind::kInteractiveFigure:
            result_.interactive_figure_annotations.push_back(cached(
                interactive_figure_cache_, page_index, object_number, [&] {
                    return interactive_figure_annotation_from_object(
                        objects_, page_index, object_number, body, &remaining_payload_bytes_);
                }));
            break;
        case AnnotationKind::kMolecule:
            result_.molecule_annotations.push_back(
                cached(molecule_cache_, page_index, object_number, [&] {
                    return molecule_annotation_from_object(page_index, object_number, body);
                }));
            break;
        case AnnotationKind::kMedia:
            result_.annotations.push_back(cached(media_cache_, page_index, object_number, [&] {
                return annotation_from_object(page_index, object_number, body);
            }));
            break;
        case AnnotationKind::kNone:
            break;
        }
        return kind;
    }

    /** @brief Returns whether the annotation limit has been reached; logs the first time. */
    bool is_full() {
        const qsizetype count = result_.annotations.size() + result_.molecule_annotations.size()
            + result_.interactive_figure_annotations.size()
            + result_.atomic_orbital_annotations.size()
            + result_.molecular_symmetry_annotations.size();
        if (count < kMaximumDetectedAnnotations) {
            return false;
        }
        if (!limit_reported_) {
            qCWarning(log_media) << "Ignoring PDF annotations beyond the limit of"
                                << kMaximumDetectedAnnotations;
            limit_reported_ = true;
        }
        return true;
    }

private:
    /** @brief Returns the memoized classification of an object. */
    AnnotationKind kind_of(int object_number, const QByteArray& body) {
        const auto known = kinds_.constFind(object_number);
        if (known != kinds_.cend()) {
            return known.value();
        }
        return kinds_.insert(object_number, classify_annotation(body)).value();
    }

    /**
     * @brief Returns the memoized annotation for @p object_number, building it on first use.
     * @return A copy carrying @p page_index.
     */
    template <typename Annotation, typename Builder>
    static Annotation cached(
        QHash<int, Annotation>& cache, int page_index, int object_number, Builder&& build) {
        auto it = cache.find(object_number);
        if (it == cache.end()) {
            it = cache.insert(object_number, build());
        }
        Annotation annotation = it.value();
        annotation.page_index = page_index;
        return annotation;
    }

    const ObjectMap& objects_;
    PdfMediaScanResult& result_;
    QHash<int, AnnotationKind> kinds_;
    QHash<int, PdfMediaAnnotation> media_cache_;
    QHash<int, PdfMoleculeAnnotation> molecule_cache_;
    QHash<int, PdfInteractiveFigureAnnotation> interactive_figure_cache_;
    QHash<int, PdfAtomicOrbitalAnnotation> atomic_orbital_cache_;
    QHash<int, PdfMolecularSymmetryAnnotation> molecular_symmetry_cache_;
    qsizetype remaining_payload_bytes_ = kMaximumTotalEmbeddedPayloadBytes;
    bool limit_reported_ = false;
};

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

/**
 * @brief Resolves an annotation's media filename against a PDF or UIL package root.
 * @return The absolute path, or an empty string when the name is empty, undeclared by the
 * package, or escapes the permitted directory.
 */
QString resolve_media_path(
    const QString& pdf_path,
    const QString& file_name,
    const QString& package_root_path,
    const QStringList& package_asset_paths) {
    if (file_name.isEmpty()) {
        return {};
    }

    const QString normalized_target = normalized_package_path(file_name);
    if (!package_root_path.isEmpty()) {
        for (const QString& asset_path : package_asset_paths) {
            if (normalized_target == normalized_package_path(asset_path)) {
                const QString candidate =
                    QFileInfo(QDir(package_root_path), normalized_target).absoluteFilePath();
                return is_path_within_directory(candidate, package_root_path) ? candidate
                                                                              : QString();
            }
        }
        return {};
    }

    const QFileInfo pdf_info(pdf_path);
    const QString candidate = QFileInfo(file_name).isAbsolute()
        ? QFileInfo(file_name).absoluteFilePath()
        : QFileInfo(pdf_info.absoluteDir(), file_name).absoluteFilePath();
    return is_path_within_directory(candidate, pdf_info.absolutePath()) ? candidate : QString();
}

/**
 * @brief Resolves media paths and extracts MP4 preview frames with FFmpeg.
 * Each distinct resolved file is decoded at most once per scan.
 */
void resolve_and_extract_media_frames(
    PdfMediaScanResult& result,
    const QString& pdf_path,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths) {
    QHash<QString, QImage> decoded_frames;
    for (PdfMediaAnnotation& annotation : result.annotations) {
        annotation.resolved_file_path = resolve_media_path(
            pdf_path, annotation.file_name, package_root_path, package_movie_asset_paths);
        if (annotation.is_mp4() && annotation.resolved_file_path.isEmpty()) {
            performance_log::record_event(QStringLiteral("pdf.media_path_rejected"), {
                {QStringLiteral("file_name"), QFileInfo(annotation.file_name).fileName()},
                {QStringLiteral("page"), annotation.page_index + 1}
            });
            continue;
        }
        if (!annotation.is_mp4()) {
            continue;
        }
        const auto decoded = decoded_frames.constFind(annotation.resolved_file_path);
        if (decoded != decoded_frames.cend()) {
            annotation.first_frame = decoded.value();
            continue;
        }

        performance_log::ScopedSpan frame_span(QStringLiteral("pdf.media_first_frame"), {
            {QStringLiteral("file_name"), QFileInfo(annotation.resolved_file_path).fileName()},
            {QStringLiteral("page"), annotation.page_index + 1}
        });
        QString error_message;
        annotation.first_frame = extract_first_video_frame(annotation.resolved_file_path, &error_message);
        decoded_frames.insert(annotation.resolved_file_path, annotation.first_frame);
        frame_span.add_field(QStringLiteral("frame_ready"), annotation.has_first_frame());
        if (!error_message.isEmpty()) {
            frame_span.add_field(QStringLiteral("error"), error_message);
            frame_span.set_outcome(QStringLiteral("failed"));
            qCInfo(log_media) << "Could not extract first MP4 frame from" << annotation.resolved_file_path << error_message;
        } else {
            frame_span.set_outcome(QStringLiteral("decoded"));
        }
    }
}

/**
 * @brief Resolves and parses molecule assets referenced by PDF annotations.
 * Each distinct resolved XYZ file is loaded at most once per scan.
 */
void resolve_and_load_molecules(
    PdfMediaScanResult& result,
    const QString& pdf_path,
    const QString& package_root_path,
    const QStringList& package_molecule_asset_paths) {
    QHash<QString, std::pair<MoleculeGeometry, QString>> loaded_molecules;
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
        const auto loaded = loaded_molecules.constFind(annotation.resolved_file_path);
        if (loaded != loaded_molecules.cend()) {
            annotation.geometry = loaded.value().first;
            annotation.error_message = loaded.value().second;
            continue;
        }
        load_xyz_molecule(
            annotation.resolved_file_path,
            &annotation.geometry,
            &annotation.error_message);
        loaded_molecules.insert(
            annotation.resolved_file_path, {annotation.geometry, annotation.error_message});
    }
}

/**
 * @brief Performs the media scan described by scan_pdf_media_annotations().
 * May throw (for example std::bad_alloc); the public wrapper converts exceptions into an empty
 * result. Checkpoints and the outcome are recorded on @p scan_span.
 */
void scan_pdf_media_annotations_into(
    PdfMediaScanResult& result,
    performance_log::ScopedSpan& scan_span,
    const QString& path,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths,
    const QStringList& package_molecule_asset_paths) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        scan_span.add_field(QStringLiteral("error_stage"), QStringLiteral("open_file"));
        scan_span.set_outcome(QStringLiteral("failed"));
        qCWarning(log_media) << "Could not scan PDF media annotations; failed to open" << path;
        return;
    }
    scan_span.checkpoint(QStringLiteral("open_file"));

    if (file.isSequential() || file.size() < 0 || file.size() > kMaximumPdfScanBytes) {
        scan_span.add_field(QStringLiteral("error_stage"), QStringLiteral("file_size_limit"));
        scan_span.set_outcome(QStringLiteral("skipped_too_large"));
        qCWarning(log_media) << "Skipping PDF media scan because the file exceeds the scan limit"
                            << "or is not a regular file" << path;
        return;
    }

    const qint64 expected_size = file.size();
    const QByteArray file_bytes = file.read(expected_size);
    if (file.error() != QFileDevice::NoError || file_bytes.size() != expected_size) {
        scan_span.add_field(QStringLiteral("error_stage"), QStringLiteral("read_file"));
        scan_span.set_outcome(QStringLiteral("failed"));
        qCWarning(log_media) << "Could not completely read PDF for media scanning" << path;
        return;
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
        qCInfo(log_media) << result.summary();
        return;
    }

    // Bodies in `objects` are views into `file_bytes`, which outlives the map.
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
        qCInfo(log_media) << result.summary();
        return;
    }

    AnnotationCollector collector(objects, result);
    std::unordered_set<int> seen_interactive_objects;
    QHash<int, QVector<int>> annotation_arrays;
    qsizetype annotation_references = 0;
    const QVector<int> pages = ordered_page_objects(objects);
    for (int page_index = 0; page_index < pages.size() && !collector.is_full(); ++page_index) {
        const QByteArray page_body = objects.value(pages.at(page_index));
        const QVector<int> page_annotations =
            annotation_objects_for_page(objects, page_body, annotation_arrays);
        if (page_annotations.size() > kMaximumAnnotationReferences - annotation_references) {
            qCWarning(log_media) << "Ignoring PDF annotation references beyond the limit of"
                                << kMaximumAnnotationReferences;
            break;
        }
        annotation_references += page_annotations.size();
        for (int annotation_object : page_annotations) {
            const AnnotationKind kind =
                collector.add(page_index, annotation_object, objects.value(annotation_object));
            if (kind != AnnotationKind::kNone) {
                seen_interactive_objects.insert(annotation_object);
            }
        }
    }
    scan_span.checkpoint(QStringLiteral("scan_page_annotations"), {
        {QStringLiteral("annotation_count"), result.annotations.size()},
        {QStringLiteral("molecule_annotation_count"), result.molecule_annotations.size()},
        {QStringLiteral("interactive_figure_annotation_count"), result.interactive_figure_annotations.size()},
        {QStringLiteral("atomic_orbital_annotation_count"), result.atomic_orbital_annotations.size()},
        {QStringLiteral("molecular_symmetry_annotation_count"), result.molecular_symmetry_annotations.size()},
        {QStringLiteral("page_count"), pages.size()}
    });

    for (auto it = objects.cbegin(); it != objects.cend() && !collector.is_full(); ++it) {
        if (seen_interactive_objects.contains(it.key())) {
            continue;
        }
        collector.add(-1, it.key(), it.value());
    }
    scan_span.checkpoint(
        QStringLiteral("scan_unattached_annotations"),
        {
            {QStringLiteral("annotation_count"), result.annotations.size()},
            {QStringLiteral("molecule_annotation_count"), result.molecule_annotations.size()},
            {QStringLiteral("interactive_figure_annotation_count"), result.interactive_figure_annotations.size()},
            {QStringLiteral("atomic_orbital_annotation_count"), result.atomic_orbital_annotations.size()},
            {QStringLiteral("molecular_symmetry_annotation_count"), result.molecular_symmetry_annotations.size()},
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
        {QStringLiteral("atomic_orbital_annotation_count"), result.atomic_orbital_annotations.size()},
        {QStringLiteral("molecular_symmetry_annotation_count"), result.molecular_symmetry_annotations.size()},
        {QStringLiteral("frames_ready"), frames_ready}
    });
    scan_span.add_field(
        QStringLiteral("annotation_count"),
        result.annotations.size() + result.molecule_annotations.size()
            + result.interactive_figure_annotations.size()
            + result.atomic_orbital_annotations.size()
            + result.molecular_symmetry_annotations.size());
    scan_span.add_field(QStringLiteral("page_count"), pages.size());
    scan_span.set_outcome(QStringLiteral("scanned"));
    qCInfo(log_media) << result.summary();
}
}  // namespace

bool PdfMediaAnnotation::has_first_frame() const {
    return !first_frame.isNull();
}

bool PdfMediaAnnotation::is_mp4() const {
    return resolved_file_path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
        || file_name.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive);
}

bool PdfMoleculeAnnotation::is_ready() const {
    return rect.isValid() && geometry.is_valid() && error_message.isEmpty();
}

bool PdfInteractiveFigureAnnotation::is_ready() const {
    return error_message.isEmpty() && definition.is_valid() && rect.isValid();
}

bool PdfAtomicOrbitalAnnotation::is_ready() const {
    return error_message.isEmpty() && definition.is_valid() && rect.isValid();
}

bool PdfMolecularSymmetryAnnotation::is_ready() const {
    return error_message.isEmpty() && definition.is_valid() && rect.isValid();
}

bool PdfMediaScanResult::has_media() const {
    return !annotations.isEmpty() || !molecule_annotations.isEmpty()
        || !interactive_figure_annotations.isEmpty()
        || !atomic_orbital_annotations.isEmpty()
        || !molecular_symmetry_annotations.isEmpty();
}

QString PdfMediaScanResult::summary() const {
    if (annotations.isEmpty() && molecule_annotations.isEmpty()
        && interactive_figure_annotations.isEmpty()
        && atomic_orbital_annotations.isEmpty()
        && molecular_symmetry_annotations.isEmpty()) {
        return QStringLiteral("No PDF media annotations detected");
    }

    QStringList parts;
    for (const PdfMediaAnnotation& annotation : annotations) {
        QString part = annotation.page_index >= 0
            ? QStringLiteral("page %1").arg(annotation.page_index + 1)
            : QStringLiteral("unknown page");
        part += QStringLiteral(" %1").arg(annotation.subtype);
        if (!annotation.file_name.isEmpty()) {
            part += QStringLiteral(" (%1)").arg(annotation.file_name);
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
    for (const PdfAtomicOrbitalAnnotation& annotation : atomic_orbital_annotations) {
        QString part = annotation.page_index >= 0
            ? QStringLiteral("page %1 atomic orbital").arg(annotation.page_index + 1)
            : QStringLiteral("unknown page atomic orbital");
        if (!annotation.file_name.isEmpty()) {
            part += QStringLiteral(" (%1)").arg(annotation.file_name);
        }
        part += annotation.is_ready() ? QStringLiteral(" [embedded orbital ready]")
                                      : QStringLiteral(" [orbital unavailable]");
        parts.push_back(part);
    }
    for (const PdfMolecularSymmetryAnnotation& annotation : molecular_symmetry_annotations) {
        QString part = annotation.page_index >= 0
            ? QStringLiteral("page %1 molecular symmetry").arg(annotation.page_index + 1)
            : QStringLiteral("unknown page molecular symmetry");
        if (!annotation.file_name.isEmpty()) part += QStringLiteral(" (%1)").arg(annotation.file_name);
        part += annotation.is_ready() ? QStringLiteral(" [embedded symmetry ready]")
                                      : QStringLiteral(" [symmetry unavailable]");
        parts.push_back(part);
    }
    return QStringLiteral("%1 interactive annotation(s): %2")
        .arg(annotations.size() + molecule_annotations.size()
             + interactive_figure_annotations.size() + atomic_orbital_annotations.size()
             + molecular_symmetry_annotations.size())
        .arg(parts.join(QStringLiteral("; ")));
}

PdfMediaScanResult scan_pdf_media_annotations(
    const QString& path,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths,
    const QStringList& package_molecule_asset_paths) noexcept {
    PdfMediaScanResult result;
    std::optional<performance_log::ScopedSpan> scan_span;
    try {
        const QFileInfo file_info(path);
        scan_span.emplace(QStringLiteral("pdf.media_scan"), QVariantMap{
            {QStringLiteral("file_name"), file_info.fileName()},
            {QStringLiteral("file_size_bytes"), file_info.size()}
        });
        scan_pdf_media_annotations_into(
            result,
            *scan_span,
            path,
            package_root_path,
            package_movie_asset_paths,
            package_molecule_asset_paths);
        return result;
    } catch (const std::exception& error) {
        result = PdfMediaScanResult();
        try {
            qCWarning(log_media) << "PDF media scan aborted for" << path
                                << "after an internal error:" << error.what();
            if (scan_span) {
                scan_span->add_field(QStringLiteral("error_stage"), QStringLiteral("exception"));
                scan_span->set_outcome(QStringLiteral("failed"));
            }
        } catch (...) {
        }
    } catch (...) {
        result = PdfMediaScanResult();
        try {
            qCWarning(log_media) << "PDF media scan aborted for" << path
                                << "after an unknown internal error";
            if (scan_span) {
                scan_span->add_field(QStringLiteral("error_stage"), QStringLiteral("exception"));
                scan_span->set_outcome(QStringLiteral("failed"));
            }
        } catch (...) {
        }
    }
    return result;
}
