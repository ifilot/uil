#pragma once

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>

namespace ui_math_text {

inline QString latex_segment_to_html(QString source, bool math_mode) {
    QString html = source.toHtmlEscaped();
    html.replace(QStringLiteral("\\left"), QString());
    html.replace(QStringLiteral("\\right"), QString());
    html.replace(QStringLiteral("\\,"), QStringLiteral("&#x2009;"));
    html.replace(QStringLiteral("\\;"), QStringLiteral("&#x2005;"));
    html.replace(QStringLiteral("\\quad"), QStringLiteral("&#x2003;"));
    html.replace(QStringLiteral("\\sum"), QStringLiteral("&Sigma;"));
    html.replace(QStringLiteral("\\pi"), QStringLiteral("&pi;"));
    html.replace(QStringLiteral("\\psi"), QStringLiteral("&psi;"));
    html.replace(QStringLiteral("\\Psi"), QStringLiteral("&Psi;"));
    html.replace(QStringLiteral("\\Phi"), QStringLiteral("&Phi;"));
    html.replace(QStringLiteral("\\alpha"), QStringLiteral("&alpha;"));
    html.replace(QStringLiteral("\\omega"), QStringLiteral("&omega;"));
    html.replace(QStringLiteral("\\tau"), QStringLiteral("&tau;"));
    html.replace(QStringLiteral("\\ell"), QStringLiteral("&#x2113;"));
    html.replace(QStringLiteral("\\hbar"), QStringLiteral("&#x210F;"));
    html.replace(
        QRegularExpression(QStringLiteral(R"(\\frac\{([^{}]+)\}\{([^{}]+)\})")),
        QStringLiteral("<span><sup>\\1</sup>&frasl;<sub>\\2</sub></span>"));
    html.replace(
        QRegularExpression(QStringLiteral(R"(\\mathrm\{([^{}]+)\})")),
        QStringLiteral("<span style='font-style:normal'>\\1</span>"));
    html.replace(
        QRegularExpression(QStringLiteral(R"(_\{([^{}]+)\})")),
        QStringLiteral("<sub>\\1</sub>"));
    html.replace(
        QRegularExpression(QStringLiteral(R"(\^\{([^{}]+)\})")),
        QStringLiteral("<sup>\\1</sup>"));
    html.replace(
        QRegularExpression(QStringLiteral(R"(_([A-Za-z0-9]))")),
        QStringLiteral("<sub>\\1</sub>"));
    html.replace(
        QRegularExpression(QStringLiteral(R"(\^([A-Za-z0-9]))")),
        QStringLiteral("<sup>\\1</sup>"));
    return math_mode ? QStringLiteral("<i>%1</i>").arg(html) : html;
}

/** Converts the small LaTeX-math subset used by UIL labels to rich text. */
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

inline QSizeF size(const QString& text, const QFont& font) {
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(QStringLiteral("<span style='white-space:nowrap'>%1</span>")
                         .arg(latex_math_html(text)));
    document.setTextWidth(-1.0);
    document.setTextWidth(document.idealWidth());
    return document.documentLayout()->documentSize();
}

inline void draw(
    QPainter& painter,
    const QRectF& bounds,
    const QString& text,
    Qt::Alignment alignment,
    const QFont& font,
    const QColor& color) {
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(QStringLiteral(
        "<span style='white-space:nowrap; color:%1'>%2</span>")
        .arg(color.name(QColor::HexRgb), latex_math_html(text)));
    document.setTextWidth(-1.0);
    document.setTextWidth(document.idealWidth());
    const QSizeF text_size = document.documentLayout()->documentSize();
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
    document.drawContents(&painter, QRectF(QPointF(0.0, 0.0), text_size));
    painter.restore();
}

}  // namespace ui_math_text
