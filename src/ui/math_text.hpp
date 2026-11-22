#pragma once

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QThread>

#include <list>
#include <memory>
#include <utility>

/**
 * Rich-text rendering of the small LaTeX-math subset used by interactive-figure and
 * atomic-orbital labels.
 *
 * Laid-out documents are kept in a small process-wide LRU cache, so repainting an unchanged
 * label replays an existing layout instead of converting LaTeX, parsing HTML, and shaping text
 * again. The cache stores vector layouts rather than rasters, so output is identical under any
 * painter transform (the vertical axis labels are drawn rotated) and device-pixel ratio.
 *
 * The cache is not synchronized: size() and draw() must only be called on the GUI thread.
 */
namespace ui_math_text {

/**
 * @brief Converts one `$`-delimited segment of a label to Qt rich text.
 * @param source Raw segment text; HTML-special characters are escaped first.
 * @param math_mode Whether the segment was inside `$...$` and should be italicized.
 * @return An HTML fragment supporting the commands, sub/superscripts, and `\frac` handled below.
 */
inline QString latex_segment_to_html(QString source, bool math_mode) {
    // Compiling a QRegularExpression is comparatively expensive; matching with a shared
    // const instance is thread-safe.
    static const QRegularExpression kFraction(QStringLiteral(R"(\\frac\{([^{}]+)\}\{([^{}]+)\})"));
    static const QRegularExpression kRoman(QStringLiteral(R"(\\mathrm\{([^{}]+)\})"));
    static const QRegularExpression kGroupedSubscript(QStringLiteral(R"(_\{([^{}]+)\})"));
    static const QRegularExpression kGroupedSuperscript(QStringLiteral(R"(\^\{([^{}]+)\})"));
    static const QRegularExpression kSubscript(QStringLiteral(R"(_([A-Za-z0-9]))"));
    static const QRegularExpression kSuperscript(QStringLiteral(R"(\^([A-Za-z0-9]))"));

    QString html = source.toHtmlEscaped();
    html.replace(QStringLiteral("\\left"), QString());
    html.replace(QStringLiteral("\\right"), QString());
    html.replace(QStringLiteral("\\,"), QStringLiteral("&#x2009;"));
    html.replace(QStringLiteral("\\;"), QStringLiteral("&#x2005;"));
    html.replace(QStringLiteral("\\quad"), QStringLiteral("&#x2003;"));
    html.replace(QStringLiteral("\\sum"), QStringLiteral("&Sigma;"));
    html.replace(QStringLiteral("\\times"), QStringLiteral("&times;"));
    html.replace(QStringLiteral("\\pi"), QStringLiteral("&pi;"));
    html.replace(QStringLiteral("\\psi"), QStringLiteral("&psi;"));
    html.replace(QStringLiteral("\\Psi"), QStringLiteral("&Psi;"));
    html.replace(QStringLiteral("\\Phi"), QStringLiteral("&Phi;"));
    html.replace(QStringLiteral("\\alpha"), QStringLiteral("&alpha;"));
    html.replace(QStringLiteral("\\omega"), QStringLiteral("&omega;"));
    html.replace(QStringLiteral("\\tau"), QStringLiteral("&tau;"));
    html.replace(QStringLiteral("\\ell"), QStringLiteral("&#x2113;"));
    html.replace(QStringLiteral("\\hbar"), QStringLiteral("&#x210F;"));
    html.replace(kFraction, QStringLiteral("<span><sup>\\1</sup>&frasl;<sub>\\2</sub></span>"));
    html.replace(kRoman, QStringLiteral("<span style='font-style:normal'>\\1</span>"));
    html.replace(kGroupedSubscript, QStringLiteral("<sub>\\1</sub>"));
    html.replace(kGroupedSuperscript, QStringLiteral("<sup>\\1</sup>"));
    html.replace(kSubscript, QStringLiteral("<sub>\\1</sub>"));
    html.replace(kSuperscript, QStringLiteral("<sup>\\1</sup>"));
    return math_mode ? QStringLiteral("<i>%1</i>").arg(html) : html;
}

/**
 * @brief Converts the small LaTeX-math subset used by UIL labels to rich text.
 *
 * Text between matched `$` pairs is italicized math; an unmatched final `$` is literal text.
 */
inline QString latex_math_html(QString source) {
    source = source.trimmed();
    const QStringList segments = source.split(QLatin1Char('$'), Qt::KeepEmptyParts);
    if (segments.size() == 1) {
        return latex_segment_to_html(source, false);
    }

    QString html;
    for (int index = 0; index < segments.size(); ++index) {
        // An unmatched final '$' is treated as ordinary text.
        const bool math_mode = index % 2 == 1 && index + 1 < segments.size();
        html += latex_segment_to_html(segments.at(index), math_mode);
    }
    return html;
}

namespace detail {

/** Maximum number of laid-out labels retained; a full figure repaint uses about twenty. */
inline constexpr int kLayoutCacheCapacity = 64;

/** @brief A laid-out, non-wrapping label document and its logical size. */
struct PreparedText {
    std::unique_ptr<QTextDocument> document;
    QSizeF size;
};

/** @brief GUI-thread LRU cache of laid-out label documents keyed by text, font, and colour. */
class LayoutCache {
public:
    /**
     * @brief Returns the laid-out document for a label, building and caching it on a miss.
     * @param color Text colour baked into the document; an invalid colour omits the colour
     *        style, which is used for size-only queries.
     * @return A reference that stays valid until the next lookup or clear().
     */
    const PreparedText& get(const QString& text, const QFont& font, const QColor& color) {
        Q_ASSERT_X(!QCoreApplication::instance() ||
                       QThread::currentThread() == QCoreApplication::instance()->thread(),
                   "ui_math_text", "math labels must be laid out on the GUI thread");
        const QString key = text + QChar(0x1f) + font.key() + QChar(0x1f) +
                            (color.isValid() ? color.name(QColor::HexArgb) : QString());
        const auto found = index_.constFind(key);
        if (found != index_.constEnd()) {
            // Move the hit to the front without invalidating stored iterators.
            entries_.splice(entries_.begin(), entries_, found.value());
            return entries_.front().second;
        }

        register_shutdown_cleanup();
        if (int(entries_.size()) >= kLayoutCacheCapacity) {
            index_.remove(entries_.back().first);
            entries_.pop_back();
        }
        entries_.emplace_front(key, prepare(text, font, color));
        index_.insert(key, entries_.begin());
        return entries_.front().second;
    }

    /** @brief Destroys every cached document. */
    void clear() {
        index_.clear();
        entries_.clear();
        cleanup_registered_ = false;
    }

private:
    using Entry = std::pair<QString, PreparedText>;

    /** @brief Lays out @p text exactly as the uncached renderer did. */
    static PreparedText prepare(const QString& text, const QFont& font, const QColor& color) {
        PreparedText prepared;
        prepared.document = std::make_unique<QTextDocument>();
        QTextDocument& document = *prepared.document;
        document.setDocumentMargin(0.0);
        document.setDefaultFont(font);
        if (color.isValid()) {
            document.setHtml(QStringLiteral("<span style='white-space:nowrap; color:%1'>%2</span>")
                                 .arg(color.name(QColor::HexRgb), latex_math_html(text)));
        } else {
            document.setHtml(QStringLiteral("<span style='white-space:nowrap'>%1</span>")
                                 .arg(latex_math_html(text)));
        }
        document.setTextWidth(-1.0);
        document.setTextWidth(document.idealWidth());
        prepared.size = document.documentLayout()->documentSize();
        return prepared;
    }

    /**
     * @brief Schedules clear() for QCoreApplication destruction.
     *
     * Documents hold font-engine references that must be released while the GUI application
     * still exists, not during static destruction after main() returns.
     */
    void register_shutdown_cleanup();

    std::list<Entry> entries_;
    QHash<QString, std::list<Entry>::iterator> index_;
    bool cleanup_registered_ = false;
};

/** @brief Returns the process-wide label layout cache. */
inline LayoutCache& layout_cache() {
    static LayoutCache cache;
    return cache;
}

/** @brief Post-routine adapter that empties the process-wide layout cache. */
inline void clear_layout_cache() { layout_cache().clear(); }

inline void LayoutCache::register_shutdown_cleanup() {
    if (cleanup_registered_) return;
    qAddPostRoutine(&clear_layout_cache);
    cleanup_registered_ = true;
}

}  // namespace detail

/**
 * @brief Returns the logical size of a rendered label without drawing it.
 *
 * GUI thread only; the layout is cached and shared with later draw() calls of the same text and
 * font.
 */
inline QSizeF size(const QString& text, const QFont& font) {
    return detail::layout_cache().get(text, font, QColor()).size;
}

/**
 * @brief Draws a label aligned inside @p bounds without wrapping or clipping it.
 *
 * Only the horizontal and vertical alignment flags of @p alignment are honoured; text larger
 * than @p bounds overflows symmetrically for centred alignment. The painter's state is restored
 * afterward. An invalid @p color draws black. GUI thread only.
 */
inline void draw(QPainter& painter, const QRectF& bounds, const QString& text,
                 Qt::Alignment alignment, const QFont& font, const QColor& color) {
    // An invalid colour has always rendered as black; keep it distinct from size-only entries.
    const detail::PreparedText& prepared =
        detail::layout_cache().get(text, font, color.isValid() ? color : QColor(Qt::black));
    const QSizeF text_size = prepared.size;
    qreal x = bounds.left();
    qreal y = bounds.top();
    if (alignment.testFlag(Qt::AlignHCenter)) {
        x += (bounds.width() - text_size.width()) / 2.0;
    } else if (alignment.testFlag(Qt::AlignRight)) {
        x += bounds.width() - text_size.width();
    }
    if (alignment.testFlag(Qt::AlignVCenter)) {
        y += (bounds.height() - text_size.height()) / 2.0;
    } else if (alignment.testFlag(Qt::AlignBottom)) {
        y += bounds.height() - text_size.height();
    }
    painter.save();
    painter.translate(x, y);
    prepared.document->drawContents(&painter, QRectF(QPointF(0.0, 0.0), text_size));
    painter.restore();
}

}  // namespace ui_math_text
