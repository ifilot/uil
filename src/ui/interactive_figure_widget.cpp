#include "ui/interactive_figure_widget.hpp"

#include "figure/harmonic_wavepacket.hpp"
#include "figure/particle_in_box_basis.hpp"

#include <QContextMenuEvent>
#include <QAbstractTextDocumentLayout>
#include <QFrame>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QSvgRenderer>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QShowEvent>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace {
constexpr int kSliderSteps = 1000;
constexpr int kAnimationIntervalMs = 16;
constexpr double kPhaseStep = 0.035;

double slider_value(int position, double minimum, double maximum) {
    return minimum + (maximum - minimum) * double(position) / double(kSliderSteps);
}

int slider_position(double value, double minimum, double maximum) {
    return int(std::lround((value - minimum) * kSliderSteps / (maximum - minimum)));
}

bool is_harmonic_time_figure(InteractiveFigureDefinition::Kind kind) {
    return kind == InteractiveFigureDefinition::Kind::HarmonicBondWavepacket
        || kind == InteractiveFigureDefinition::Kind::HarmonicBasisStates;
}

bool is_box_basis_figure(InteractiveFigureDefinition::Kind kind) {
    return kind == InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion;
}

QString latex_math_html(QString source) {
    source = source.trimmed();
    const bool math_mode = source.size() >= 2
        && source.startsWith(QLatin1Char('$'))
        && source.endsWith(QLatin1Char('$'));
    if (math_mode) {
        source = source.mid(1, source.size() - 2);
    }
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
    if (math_mode) {
        html = QStringLiteral("<i>%1</i>").arg(html);
    }
    return html;
}

QSizeF math_text_size(const QString& text, const QFont& font) {
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(QStringLiteral("<span style='white-space:nowrap'>%1</span>")
                         .arg(latex_math_html(text)));
    document.setTextWidth(-1.0);
    document.setTextWidth(document.idealWidth());
    return document.documentLayout()->documentSize();
}

void draw_math_text(
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
}

class InteractiveFigureWidget::PlotCanvas final : public QWidget {
public:
    explicit PlotCanvas(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumSize(240, 140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void set_definition(const InteractiveFigureDefinition& definition) {
        definition_ = definition;
        renderer_.load(definition.background_svg);
        amplitude_ = definition.amplitude_initial;
        frequency_ = definition.frequency_initial;
        harmonic_phase_ = definition.phase_initial;
        stretch_ = definition.stretch_initial;
        basis_count_ = definition.basis_count_initial;
        phase_ = 0.0;
        update();
    }

    void set_amplitude(double value) { amplitude_ = value; update(); }
    void set_frequency(double value) { frequency_ = value; update(); }
    void advance_phase() { phase_ = std::fmod(phase_ + kPhaseStep, 2.0 * std::numbers::pi); update(); }
    void reset_phase() { phase_ = 0.0; update(); }
    void set_harmonic_phase(double value) { harmonic_phase_ = value; update(); }
    void set_stretch(double value) { stretch_ = value; update(); }
    void set_basis_count(int value) { basis_count_ = value; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), Qt::white);
        if (renderer_.isValid()) {
            renderer_.render(&painter, QRectF(rect()));
        }
        if (definition_.kind == InteractiveFigureDefinition::Kind::HarmonicBondWavepacket) {
            paint_harmonic_wavepacket(painter);
            return;
        }
        if (definition_.kind == InteractiveFigureDefinition::Kind::HarmonicBasisStates) {
            paint_harmonic_basis_states(painter);
            return;
        }
        if (is_box_basis_figure(definition_.kind)) {
            paint_particle_in_box_step_expansion(painter);
            return;
        }

        QFont title_font = painter.font();
        title_font.setBold(true);
        title_font.setPixelSize(std::clamp(height() / 15, 18, 40));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_height = title_metrics.height() + 10.0;
        const qreal title_top = 5.0;
        const qreal left_margin = std::clamp(width() / 16, 78, 140);
        const qreal right_margin = std::max(34, width() / 40);
        const qreal bottom_margin = std::clamp(height() / 6, 62, 120);
        const QRectF plot_rect = QRectF(rect()).adjusted(
            left_margin,
            title_top + title_height + 8.0,
            -right_margin,
            -bottom_margin);
        if (!plot_rect.isValid()) {
            return;
        }

        painter.setPen(QColor(15, 23, 42));
        painter.setFont(title_font);
        const QString displayed_title = title_metrics.elidedText(
            definition_.title, Qt::ElideRight, std::max(0.0, width() - 32.0));
        painter.drawText(
            QRectF(16.0, title_top, width() - 32.0, title_height),
            Qt::AlignHCenter | Qt::AlignVCenter,
            displayed_title);

        const auto map_point = [&](double x, double y) {
            return QPointF(
                plot_rect.left() + (x - definition_.x_min)
                    / (definition_.x_max - definition_.x_min) * plot_rect.width(),
                plot_rect.bottom() - (y - definition_.y_min)
                    / (definition_.y_max - definition_.y_min) * plot_rect.height());
        };

        painter.save();
        painter.setClipRect(plot_rect);
        painter.setPen(QPen(QColor(30, 41, 59, 35), 1.0));
        for (int i = 0; i <= 8; ++i) {
            const qreal x = plot_rect.left() + plot_rect.width() * i / 8.0;
            painter.drawLine(QPointF(x, plot_rect.top()), QPointF(x, plot_rect.bottom()));
        }
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot_rect.top() + plot_rect.height() * i / 4.0;
            painter.drawLine(QPointF(plot_rect.left(), y), QPointF(plot_rect.right(), y));
        }

        painter.setPen(QPen(QColor(51, 65, 85), 1.4));
        if (definition_.x_min <= 0.0 && definition_.x_max >= 0.0) {
            painter.drawLine(map_point(0.0, definition_.y_min), map_point(0.0, definition_.y_max));
        }
        if (definition_.y_min <= 0.0 && definition_.y_max >= 0.0) {
            painter.drawLine(map_point(definition_.x_min, 0.0), map_point(definition_.x_max, 0.0));
        }

        QPainterPath curve;
        constexpr int samples = 600;
        for (int sample = 0; sample <= samples; ++sample) {
            const double x = definition_.x_min
                + (definition_.x_max - definition_.x_min) * sample / samples;
            const double y = amplitude_ * std::sin(frequency_ * x - phase_);
            const QPointF point = map_point(x, y);
            if (sample == 0) {
                curve.moveTo(point);
            } else {
                curve.lineTo(point);
            }
        }
        painter.setPen(QPen(definition_.curve_color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(curve);
        painter.restore();

        QFont tick_font = painter.font();
        tick_font.setBold(false);
        tick_font.setPixelSize(std::clamp(height() / 25, 14, 32));
        painter.setFont(tick_font);
        painter.setPen(QColor(71, 85, 105));
        const QFontMetricsF tick_metrics(tick_font);
        for (int i = 0; i <= 8; ++i) {
            const double value = definition_.x_min
                + (definition_.x_max - definition_.x_min) * i / 8.0;
            const QString text = QString::number(value, 'g', 3);
            const qreal x = plot_rect.left() + plot_rect.width() * i / 8.0;
            painter.drawText(
                QRectF(x - 30.0, plot_rect.bottom() + 4.0, 60.0, tick_metrics.height()),
                Qt::AlignHCenter | Qt::AlignTop,
                text);
        }
        for (int i = 0; i <= 4; ++i) {
            const double value = definition_.y_max
                - (definition_.y_max - definition_.y_min) * i / 4.0;
            painter.drawText(
                QRectF(27.0,
                       plot_rect.top() + plot_rect.height() * i / 4.0
                           - tick_metrics.height() / 2.0,
                       plot_rect.left() - 39.0,
                       tick_metrics.height()),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(value, 'g', 3));
        }

        QFont axis_font = tick_font;
        axis_font.setBold(true);
        axis_font.setPixelSize(std::clamp(height() / 21, 18, 38));
        const QFontMetricsF axis_metrics(axis_font);
        painter.setFont(axis_font);
        painter.setPen(QColor(30, 41, 59));
        draw_math_text(
            painter,
            QRectF(plot_rect.left(), height() - axis_metrics.height() - 5.0,
                   plot_rect.width(), axis_metrics.height()),
            definition_.x_label,
            Qt::AlignCenter,
            axis_font,
            QColor(30, 41, 59));
        painter.save();
        painter.translate(18.0, plot_rect.center().y());
        painter.rotate(-90.0);
        draw_math_text(
            painter,
            QRectF(-plot_rect.height() / 2.0, -axis_metrics.height() / 2.0,
                   plot_rect.height(), axis_metrics.height()),
            definition_.y_label,
            Qt::AlignCenter,
            axis_font,
            QColor(30, 41, 59));
        painter.restore();
    }

private:
    void paint_harmonic_wavepacket(QPainter& painter) {
        const HarmonicWavepacketObservation observation = evaluate_harmonic_wavepacket(
            harmonic_phase_, stretch_);

        QFont title_font = painter.font();
        title_font.setBold(true);
        title_font.setPixelSize(std::clamp(height() / 20, 18, 34));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_top = 3.0;
        const qreal title_height = title_metrics.height() + 6.0;
        painter.setFont(title_font);
        painter.setPen(QColor(15, 23, 42));
        painter.drawText(
            QRectF(16.0, title_top, width() - 32.0, title_height),
            Qt::AlignCenter,
            title_metrics.elidedText(definition_.title, Qt::ElideRight, width() - 32));

        const qreal left_margin = std::clamp(width() / 13, 90, 150);
        const qreal right_margin = std::max(34, width() / 35);
        const qreal bottom_margin = std::clamp(height() / 9, 58, 105);
        const qreal plot_gap = std::clamp(height() / 11, 45, 80);
        const qreal plot_top = title_top + title_height + 5.0;
        const qreal available_height = height() - plot_top - bottom_margin - plot_gap;
        if (available_height < 100.0) {
            return;
        }
        const qreal plot_height = available_height / 2.0;
        const QRectF upper(left_margin, plot_top,
                           width() - left_margin - right_margin, plot_height);
        const QRectF lower(left_margin, upper.bottom() + plot_gap,
                           upper.width(), plot_height);

        const auto map_x = [&](double x) {
            return upper.left() + (x - definition_.x_min)
                / (definition_.x_max - definition_.x_min) * upper.width();
        };
        const auto map_upper_y = [&](double y) {
            return upper.bottom() - y / definition_.potential_y_max * upper.height();
        };
        const auto map_lower_y = [&](double y) {
            return lower.bottom() - y / definition_.density_y_max * lower.height();
        };

        const auto draw_grid = [&](const QRectF& area) {
            painter.save();
            painter.setClipRect(area);
            painter.setPen(QPen(QColor(71, 85, 105, 35), 1.0));
            for (int i = 0; i <= 8; ++i) {
                const qreal x = area.left() + area.width() * i / 8.0;
                painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            }
            for (int i = 0; i <= 4; ++i) {
                const qreal y = area.top() + area.height() * i / 4.0;
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            }
            painter.setPen(QPen(QColor(71, 85, 105), 1.2));
            painter.drawLine(area.bottomLeft(), area.bottomRight());
            painter.drawLine(area.topLeft(), area.bottomLeft());
            painter.restore();
        };
        draw_grid(upper);
        draw_grid(lower);

        painter.save();
        painter.setClipRect(upper);
        QPainterPath potential;
        constexpr int potential_samples = 500;
        for (int i = 0; i <= potential_samples; ++i) {
            const double x = definition_.x_min
                + (definition_.x_max - definition_.x_min) * i / potential_samples;
            const QPointF point(map_x(x), map_upper_y(0.5 * x * x));
            i == 0 ? potential.moveTo(point) : potential.lineTo(point);
        }
        painter.setPen(QPen(definition_.potential_color, 2.2));
        painter.drawPath(potential);
        if (definition_.show_energy_reference) {
            const double energy = 0.5 * stretch_ * stretch_;
            if (energy <= definition_.potential_y_max) {
                painter.setPen(QPen(QColor(100, 116, 139, 125), 1.2, Qt::DashLine));
                painter.drawLine(QPointF(upper.left(), map_upper_y(energy)),
                                 QPointF(upper.right(), map_upper_y(energy)));
            }
        }
        const qreal marker_x = map_x(observation.mean_position);
        const qreal marker_y = map_upper_y(observation.potential_at_mean);
        painter.setPen(QPen(QColor(100, 116, 139, 150), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(marker_x, upper.bottom()), QPointF(marker_x, marker_y));
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(definition_.marker_color);
        painter.drawEllipse(QPointF(marker_x, marker_y), 6.0, 6.0);
        painter.restore();

        const QVector<QPointF> density = sample_harmonic_probability_density(
            definition_.x_min, definition_.x_max, 500, observation.mean_position);
        painter.save();
        painter.setClipRect(lower);
        QPainterPath fill;
        fill.moveTo(lower.left(), lower.bottom());
        QPainterPath outline;
        for (int i = 0; i < density.size(); ++i) {
            const QPointF point(map_x(density.at(i).x()), map_lower_y(density.at(i).y()));
            if (i == 0) outline.moveTo(point); else outline.lineTo(point);
            fill.lineTo(point);
        }
        fill.lineTo(lower.right(), lower.bottom());
        fill.closeSubpath();
        QColor density_fill = definition_.density_color;
        density_fill.setAlpha(65);
        painter.fillPath(fill, density_fill);
        painter.setPen(QPen(definition_.density_color, 2.4));
        painter.drawPath(outline);
        painter.setPen(QPen(definition_.marker_color, 1.4, Qt::DashLine));
        painter.drawLine(QPointF(marker_x, lower.top()), QPointF(marker_x, lower.bottom()));
        painter.restore();

        QFont text_font = painter.font();
        text_font.setBold(false);
        text_font.setPixelSize(std::clamp(height() / 29, 14, 28));
        painter.setFont(text_font);
        painter.setPen(QColor(71, 85, 105));
        const QFontMetricsF metrics(text_font);
        for (int i = 0; i <= 8; ++i) {
            const double value = definition_.x_min
                + (definition_.x_max - definition_.x_min) * i / 8.0;
            const qreal x = lower.left() + lower.width() * i / 8.0;
            painter.drawText(QRectF(x - 28.0, lower.bottom() + 3.0, 56.0, metrics.height()),
                             Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(value, 'g', 3));
        }
        const auto draw_y_ticks = [&](const QRectF& area, double maximum) {
            for (int i = 0; i <= 2; ++i) {
                const double value = maximum * (2 - i) / 2.0;
                const qreal y = area.top() + area.height() * i / 2.0;
                painter.drawText(QRectF(31.0, y - metrics.height() / 2.0,
                                        area.left() - 43.0, metrics.height()),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(value, 'g', 3));
            }
        };
        draw_y_ticks(upper, definition_.potential_y_max);
        draw_y_ticks(lower, definition_.density_y_max);

        QFont label_font = text_font;
        label_font.setBold(true);
        label_font.setPixelSize(std::clamp(height() / 25, 18, 34));
        const QFontMetricsF label_metrics(label_font);
        painter.setFont(label_font);
        painter.setPen(QColor(30, 41, 59));
        draw_math_text(
            painter,
            QRectF(lower.left(), height() - label_metrics.height() - 8.0,
                   lower.width(), label_metrics.height() + 4.0),
            definition_.x_label,
            Qt::AlignCenter,
            label_font,
            QColor(30, 41, 59));
        const auto draw_vertical_label = [&](const QRectF& area, const QString& label) {
            painter.save();
            painter.translate(18.0, area.center().y());
            painter.rotate(-90.0);
            draw_math_text(
                painter,
                QRectF(-area.height() / 2.0, -label_metrics.height() / 2.0 - 2.0,
                       area.height(), label_metrics.height() + 4.0),
                label,
                Qt::AlignCenter,
                label_font,
                QColor(30, 41, 59));
            painter.restore();
        };
        draw_vertical_label(upper, definition_.potential_label);
        draw_vertical_label(lower, definition_.density_label);

        painter.setFont(text_font);
        painter.setPen(QColor(51, 65, 85));
        painter.drawText(QRectF(upper.left() + 7.0, upper.top() + 4.0,
                                upper.width() - 14.0, metrics.height()),
                         Qt::AlignLeft | Qt::AlignTop, QStringLiteral("Harmonic potential"));
        painter.drawText(QRectF(lower.left() + 7.0, lower.top() + 4.0,
                                lower.width() - 14.0, metrics.height()),
                         Qt::AlignLeft | Qt::AlignTop, QStringLiteral("Probability density"));
    }

    void paint_harmonic_basis_states(QPainter& painter) {
        constexpr int basis_count = 6;
        QFont title_font = painter.font();
        title_font.setBold(true);
        title_font.setPixelSize(std::clamp(height() / 20, 18, 34));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_top = 3.0;
        const qreal title_height = title_metrics.height() + 6.0;
        painter.setFont(title_font);
        painter.setPen(QColor(15, 23, 42));
        painter.drawText(QRectF(16.0, title_top, width() - 32.0, title_height),
                         Qt::AlignCenter,
                         title_metrics.elidedText(definition_.title, Qt::ElideRight, width() - 32));

        const qreal left_margin = std::clamp(width() / 16, 70, 125);
        const qreal right_margin = std::max(20, width() / 45);
        const qreal bottom_margin = std::clamp(height() / 11, 48, 80);
        const qreal row_gap = std::clamp(height() / 45, 10, 20);
        const qreal column_gap = std::max(14, width() / 65);
        const qreal top = title_top + title_height + 3.0;
        const qreal available = height() - top - bottom_margin;
        if (available < 130.0) {
            return;
        }
        const qreal full_width = width() - left_margin - right_margin;
        const qreal coherent_height = available * 0.29;
        const qreal grid_top = top + coherent_height + row_gap * 1.45;
        const qreal component_height = (height() - bottom_margin - grid_top - row_gap) / 2.0;
        const qreal component_width = (full_width - 2.0 * column_gap) / 3.0;
        const QRectF coherent_panel(left_margin, top, full_width, coherent_height);

        QFont text_font = painter.font();
        text_font.setBold(false);
        text_font.setPixelSize(std::clamp(height() / 30, 16, 26));
        const QFontMetricsF metrics(text_font);
        painter.setFont(text_font);

        const auto map_x = [&](const QRectF& panel, double x) {
            return panel.left() + (x - definition_.x_min)
                / (definition_.x_max - definition_.x_min) * panel.width();
        };

        const auto draw_grid = [&](const QRectF& panel, bool centered_axis) {
            painter.setPen(QPen(QColor(71, 85, 105, 30), 1.0));
            for (int i = 0; i <= 4; ++i) {
                const qreal x = panel.left() + panel.width() * i / 4.0;
                painter.drawLine(QPointF(x, panel.top()), QPointF(x, panel.bottom()));
            }
            for (int i = 0; i <= 2; ++i) {
                const qreal y = panel.top() + panel.height() * i / 2.0;
                painter.drawLine(QPointF(panel.left(), y), QPointF(panel.right(), y));
            }
            painter.setPen(QPen(QColor(71, 85, 105), 1.0));
            painter.drawRect(panel);
            if (centered_axis) {
                painter.drawLine(QPointF(panel.left(), panel.center().y()),
                                 QPointF(panel.right(), panel.center().y()));
            }
        };

        const HarmonicWavepacketObservation observation = evaluate_harmonic_wavepacket(
            harmonic_phase_, stretch_);
        painter.save();
        painter.setClipRect(coherent_panel);
        draw_grid(coherent_panel, false);
        QPainterPath density_fill;
        QPainterPath density_outline;
        density_fill.moveTo(coherent_panel.left(), coherent_panel.bottom());
        constexpr int samples = 420;
        for (int i = 0; i <= samples; ++i) {
            const double x = definition_.x_min
                + (definition_.x_max - definition_.x_min) * i / samples;
            const double density = harmonic_probability_density(x, observation.mean_position);
            const QPointF point(map_x(coherent_panel, x), coherent_panel.bottom()
                - density / definition_.density_y_max * coherent_panel.height());
            if (i == 0) density_outline.moveTo(point); else density_outline.lineTo(point);
            density_fill.lineTo(point);
        }
        density_fill.lineTo(coherent_panel.right(), coherent_panel.bottom());
        density_fill.closeSubpath();
        QColor density_fill_color = definition_.density_color;
        density_fill_color.setAlpha(52);
        painter.fillPath(density_fill, density_fill_color);
        painter.setPen(QPen(definition_.density_color, 2.2));
        painter.drawPath(density_outline);
        painter.setPen(QPen(definition_.marker_color, 1.2, Qt::DashLine));
        const qreal mean_x = map_x(coherent_panel, observation.mean_position);
        painter.drawLine(QPointF(mean_x, coherent_panel.top()),
                         QPointF(mean_x, coherent_panel.bottom()));
        painter.restore();
        const QRectF coherent_caption(
            coherent_panel.left() + 5.0,
            coherent_panel.top() + 4.0,
            coherent_panel.width() - 10.0,
            metrics.height() + 6.0);
        draw_math_text(
            painter,
            coherent_caption.adjusted(5.0, 0.0, -5.0, 0.0),
            QStringLiteral("$\\mathrm{Coherent-state density}\\quad X=%1\\ell$")
                .arg(observation.mean_position, 0, 'f', 2),
            Qt::AlignLeft | Qt::AlignVCenter,
            text_font,
            QColor(30, 41, 59));

        double component_limit = 0.0;
        for (int n = 0; n < basis_count; ++n) {
            for (int i = 0; i <= 240; ++i) {
                const double x = definition_.x_min
                    + (definition_.x_max - definition_.x_min) * i / 240.0;
                component_limit = std::max(component_limit,
                    std::sqrt(coherent_state_basis_weight(n, stretch_))
                        * std::abs(harmonic_basis_wavefunction(n, x)));
            }
        }
        component_limit = std::max(0.08, component_limit * 1.12);

        for (int n = 0; n < basis_count; ++n) {
            const int row = n / 3;
            const int column = n % 3;
            const QRectF panel(
                left_margin + column * (component_width + column_gap),
                grid_top + row * (component_height + row_gap),
                component_width,
                component_height);
            painter.save();
            painter.setClipRect(panel);
            draw_grid(panel, true);
            QPainterPath outline;
            for (int i = 0; i <= samples; ++i) {
                const double x = definition_.x_min
                    + (definition_.x_max - definition_.x_min) * i / samples;
                const double component = coherent_state_basis_real_component(
                    n, x, harmonic_phase_, stretch_);
                const QPointF point(map_x(panel, x), panel.center().y()
                    - component / component_limit * panel.height() / 2.0);
                if (i == 0) outline.moveTo(point); else outline.lineTo(point);
            }
            QColor color = definition_.basis_colors.value(n, definition_.density_color);
            painter.setPen(QPen(color, 2.0));
            painter.drawPath(outline);
            painter.restore();

            const double weight = coherent_state_basis_weight(n, stretch_);
            const QRectF component_caption(
                panel.left() + 4.0,
                panel.top() + 3.0,
                panel.width() - 8.0,
                metrics.height() + 5.0);
            draw_math_text(
                painter,
                component_caption.adjusted(4.0, 0.0, -4.0, 0.0),
                QStringLiteral("$n=%1\\quad |c_{%1}|^2=%2%$")
                    .arg(n)
                    .arg(100.0 * weight, 0, 'f', 1),
                Qt::AlignLeft | Qt::AlignVCenter,
                text_font,
                QColor(30, 41, 59));
        }

        const QRectF bottom_left(left_margin, grid_top + component_height + row_gap,
                                 component_width, component_height);
        painter.setFont(text_font);
        painter.setPen(QColor(71, 85, 105));
        for (int column = 0; column < 3; ++column) {
            const QRectF panel(
                left_margin + column * (component_width + column_gap),
                bottom_left.top(), component_width, component_height);
            for (int i = 0; i <= 2; ++i) {
            const double value = definition_.x_min
                    + (definition_.x_max - definition_.x_min) * i / 2.0;
                const qreal x = panel.left() + panel.width() * i / 2.0;
                painter.drawText(
                    QRectF(x - 22.0, panel.bottom() + 1.0, 44.0, metrics.height()),
                    Qt::AlignHCenter | Qt::AlignTop,
                    QString::number(value, 'g', 3));
            }
        }
        QFont label_font = text_font;
        label_font.setBold(true);
        label_font.setPixelSize(std::clamp(height() / 28, 16, 28));
        const QFontMetricsF label_metrics(label_font);
        painter.setFont(label_font);
        painter.setPen(QColor(30, 41, 59));
        draw_math_text(
            painter,
            QRectF(left_margin, height() - label_metrics.height() - 7.0,
                   full_width, label_metrics.height() + 4.0),
            definition_.x_label,
            Qt::AlignCenter,
            label_font,
            QColor(30, 41, 59));
        painter.save();
        painter.translate(17.0, grid_top + component_height + row_gap / 2.0);
        painter.rotate(-90.0);
        draw_math_text(
            painter,
            QRectF(-(2.0 * component_height + row_gap) / 2.0,
                   -label_metrics.height() / 2.0 - 2.0,
                   2.0 * component_height + row_gap, label_metrics.height() + 4.0),
            QStringLiteral("$\\mathrm{Re}[c_n \\psi_n(x)e^{-in\\omega t}]$"),
            Qt::AlignCenter,
            label_font,
            QColor(30, 41, 59));
        painter.restore();
    }

    void paint_particle_in_box_step_expansion(QPainter& painter) {
        QFont title_font = painter.font();
        title_font.setBold(true);
        title_font.setPixelSize(std::clamp(height() / 17, 20, 38));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_top = 5.0;
        const qreal title_height = title_metrics.height() + 8.0;
        painter.setFont(title_font);
        painter.setPen(QColor(15, 23, 42));
        painter.drawText(
            QRectF(16.0, title_top, width() - 32.0, title_height),
            Qt::AlignCenter,
            title_metrics.elidedText(definition_.title, Qt::ElideRight, width() - 32));

        const qreal left_margin = std::clamp(width() / 14, 90, 150);
        const qreal right_margin = std::max(30, width() / 38);
        const qreal bottom_margin = std::clamp(height() / 6, 75, 140);
        const QRectF plot_rect = QRectF(rect()).adjusted(
            left_margin,
            title_top + title_height + 9.0,
            -right_margin,
            -bottom_margin);
        if (!plot_rect.isValid()) {
            return;
        }

        const auto map_point = [&](double x, double y) {
            return QPointF(
                plot_rect.left() + x * plot_rect.width(),
                plot_rect.bottom() - (y - definition_.y_min)
                    / (definition_.y_max - definition_.y_min) * plot_rect.height());
        };

        painter.save();
        painter.setClipRect(plot_rect);
        painter.setPen(QPen(QColor(71, 85, 105, 35), 1.0));
        for (int i = 0; i <= 4; ++i) {
            const qreal x = plot_rect.left() + plot_rect.width() * i / 4.0;
            painter.drawLine(QPointF(x, plot_rect.top()), QPointF(x, plot_rect.bottom()));
        }
        for (int i = 0; i <= 6; ++i) {
            const qreal y = plot_rect.top() + plot_rect.height() * i / 6.0;
            painter.drawLine(QPointF(plot_rect.left(), y), QPointF(plot_rect.right(), y));
        }
        painter.setPen(QPen(QColor(71, 85, 105), 1.2));
        painter.drawRect(plot_rect);
        if (definition_.y_min <= 0.0 && definition_.y_max >= 0.0) {
            painter.drawLine(map_point(0.0, 0.0), map_point(1.0, 0.0));
        }

        QPainterPath target;
        target.moveTo(map_point(0.0, 0.0));
        target.lineTo(map_point(definition_.step_position, 0.0));
        target.lineTo(map_point(definition_.step_position, definition_.step_height));
        target.lineTo(map_point(1.0, definition_.step_height));
        painter.setPen(QPen(definition_.target_color, 2.2, Qt::DashLine));
        painter.drawPath(target);

        const int sample_count = std::max(800, basis_count_ * 40 + 1);
        const QVector<QPointF> samples = sample_particle_in_box_step_approximation(
            basis_count_, sample_count, definition_.step_position, definition_.step_height);
        QPainterPath approximation;
        for (int i = 0; i < samples.size(); ++i) {
            const QPointF point = map_point(samples.at(i).x(), samples.at(i).y());
            if (i == 0) approximation.moveTo(point); else approximation.lineTo(point);
        }
        painter.setPen(QPen(
            definition_.approximation_color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(approximation);
        painter.restore();

        QFont tick_font = painter.font();
        tick_font.setBold(false);
        tick_font.setPixelSize(std::clamp(height() / 25, 18, 36));
        painter.setFont(tick_font);
        painter.setPen(QColor(71, 85, 105));
        const QFontMetricsF tick_metrics(tick_font);
        const QStringList x_tick_labels = {
            QStringLiteral("0"), QStringLiteral("L/4"), QStringLiteral("L/2"),
            QStringLiteral("3L/4"), QStringLiteral("L")};
        for (int i = 0; i <= 4; ++i) {
            const qreal x = plot_rect.left() + plot_rect.width() * i / 4.0;
            draw_math_text(
                painter,
                QRectF(x - 46.0, plot_rect.bottom() + 5.0,
                       92.0, tick_metrics.height() + 8.0),
                x_tick_labels.at(i),
                Qt::AlignHCenter | Qt::AlignTop,
                tick_font,
                QColor(71, 85, 105));
        }
        for (int i = 0; i <= 4; ++i) {
            const double value = definition_.y_max
                - (definition_.y_max - definition_.y_min) * i / 4.0;
            const qreal y = plot_rect.top() + plot_rect.height() * i / 4.0;
            painter.drawText(
                QRectF(24.0, y - tick_metrics.height() / 2.0,
                       plot_rect.left() - 36.0, tick_metrics.height()),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(value, 'g', 3));
        }

        QFont label_font = tick_font;
        label_font.setBold(true);
        label_font.setPixelSize(std::clamp(height() / 21, 20, 42));
        const QFontMetricsF label_metrics(label_font);
        painter.setFont(label_font);
        painter.setPen(QColor(30, 41, 59));
        draw_math_text(
            painter,
            QRectF(plot_rect.left(), plot_rect.bottom() + tick_metrics.height() + 15.0,
                   plot_rect.width(),
                   height() - plot_rect.bottom() - tick_metrics.height() - 23.0),
            definition_.x_label,
            Qt::AlignCenter,
            label_font,
            QColor(30, 41, 59));
        painter.save();
        painter.translate(18.0, plot_rect.center().y());
        painter.rotate(-90.0);
        draw_math_text(
            painter,
            QRectF(-plot_rect.height() / 2.0, -label_metrics.height() / 2.0,
                   plot_rect.height(), label_metrics.height() + 2.0),
            definition_.y_label,
            Qt::AlignCenter,
            label_font,
            QColor(30, 41, 59));
        painter.restore();

        const qreal legend_left = plot_rect.left() + 12.0;
        const qreal legend_top = plot_rect.top() + 9.0;
        const qreal legend_row_height = tick_metrics.height() + 8.0;
        painter.setPen(QPen(definition_.target_color, 2.2, Qt::DashLine));
        painter.drawLine(QPointF(legend_left, legend_top + legend_row_height / 2.0),
                         QPointF(legend_left + 40.0, legend_top + legend_row_height / 2.0));
        draw_math_text(
            painter,
            QRectF(legend_left + 50.0, legend_top, 300.0, legend_row_height),
            QStringLiteral("target f(x)"),
            Qt::AlignLeft | Qt::AlignVCenter,
            tick_font,
            QColor(51, 65, 85));
        painter.setPen(QPen(definition_.approximation_color, 3.0));
        painter.drawLine(
            QPointF(legend_left, legend_top + 1.5 * legend_row_height),
            QPointF(legend_left + 40.0, legend_top + 1.5 * legend_row_height));
        draw_math_text(
            painter,
            QRectF(legend_left + 50.0, legend_top + legend_row_height,
                   300.0, legend_row_height),
            QStringLiteral("$S_{%1}(x)$").arg(basis_count_),
            Qt::AlignLeft | Qt::AlignVCenter,
            tick_font,
            QColor(51, 65, 85));

        const double percentage = 100.0 * particle_in_box_step_captured_fraction(
            basis_count_, definition_.step_position, definition_.step_height);
        QFont percentage_font = label_font;
        percentage_font.setPixelSize(std::clamp(height() / 20, 24, 44));
        const QString basis_text = QStringLiteral("$N = %1$").arg(basis_count_);
        const QString fit_text = QStringLiteral("$%1% \\mathrm{fit}$")
            .arg(percentage, 0, 'f', 1);
        const QSizeF basis_text_size = math_text_size(basis_text, percentage_font);
        const QSizeF fit_text_size = math_text_size(fit_text, percentage_font);
        const qreal label_gap = std::clamp(
            2.0 * percentage_font.pixelSize(), 56.0, 96.0);
        const qreal label_bottom = plot_rect.bottom() - 8.0;
        const QRectF fit_rect(
            plot_rect.right() - fit_text_size.width() - 10.0,
            label_bottom - fit_text_size.height(),
            fit_text_size.width(),
            fit_text_size.height());
        const QRectF basis_rect(
            fit_rect.left() - label_gap - basis_text_size.width(),
            label_bottom - basis_text_size.height(),
            basis_text_size.width(),
            basis_text_size.height());
        draw_math_text(
            painter,
            basis_rect,
            basis_text,
            Qt::AlignLeft | Qt::AlignTop,
            percentage_font,
            definition_.approximation_color);
        draw_math_text(
            painter,
            fit_rect,
            fit_text,
            Qt::AlignLeft | Qt::AlignTop,
            percentage_font,
            definition_.approximation_color);
    }

    InteractiveFigureDefinition definition_;
    QSvgRenderer renderer_;
    double amplitude_ = 1.0;
    double frequency_ = 1.0;
    double phase_ = 0.0;
    double harmonic_phase_ = 0.0;
    double stretch_ = 3.0;
    int basis_count_ = 1;
};

InteractiveFigureWidget::InteractiveFigureWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("interactiveFigureWidget"));
    setAutoFillBackground(true);
    QFont audience_font = font();
    audience_font.setPixelSize(28);
    setFont(audience_font);
    setStyleSheet(QStringLiteral(
        "InteractiveFigureWidget { background: white; border: 1px solid #94a3b8; }"
        "QFrame#figureControls { background: #f1f5f9; border-top: 1px solid #cbd5e1; }"
        "QLabel#figureAmplitudeLabel, QLabel#figureFrequencyLabel {"
        " background: transparent; color: #1e293b; border: none; padding: 0 3px;"
        " min-width: 270px; font-size: 26px; font-weight: 600; }"
        "QSlider#figureAmplitudeSlider, QSlider#figureFrequencySlider {"
        " background: transparent; }"
        "QSlider#figureAmplitudeSlider::groove:horizontal,"
        "QSlider#figureFrequencySlider::groove:horizontal {"
        " height: 9px; background: #cbd5e1; border-radius: 4px; }"
        "QSlider#figureAmplitudeSlider::sub-page:horizontal,"
        "QSlider#figureFrequencySlider::sub-page:horizontal {"
        " background: #0891b2; border-radius: 2px; }"
        "QSlider#figureAmplitudeSlider::handle:horizontal,"
        "QSlider#figureFrequencySlider::handle:horizontal {"
        " width: 24px; margin: -9px 0; background: #0e7490;"
        " border: 1px solid #155e75; border-radius: 12px; }"
        "QLabel#figureStatusLabel { background: transparent; color: #334155;"
        " border: none; padding: 6px 8px; font-size: 26px; font-weight: 600; }"
        "QWidget#figureControlsRow { background: transparent; }"
        "QPushButton#figureAnimationButton, QPushButton#figureResetButton {"
        " background: #0f766e; color: white; border: 1px solid #0f766e;"
        " padding: 9px 16px; min-width: 112px; font-size: 24px; font-weight: 600; }"
        "QPushButton#figureAnimationButton:hover, QPushButton#figureResetButton:hover {"
        " background: #0d9488; border-color: #0d9488; }"));

    root_layout_ = new QVBoxLayout(this);
    root_layout_->setContentsMargins(0, 0, 0, 0);
    root_layout_->setSpacing(0);
    canvas_ = new PlotCanvas(this);
    canvas_->setObjectName(QStringLiteral("interactiveFigureCanvas"));
    root_layout_->addWidget(canvas_, 1);

    controls_frame_ = new QFrame(this);
    controls_frame_->setObjectName(QStringLiteral("figureControls"));
    auto* controls_outer_layout = new QVBoxLayout(controls_frame_);
    controls_outer_layout->setContentsMargins(16, 8, 16, 12);
    controls_outer_layout->setSpacing(6);
    status_label_ = new QLabel(controls_frame_);
    status_label_->setObjectName(QStringLiteral("figureStatusLabel"));
    status_label_->setAlignment(Qt::AlignCenter);
    status_label_->setTextFormat(Qt::RichText);
    status_label_->setWordWrap(true);
    status_label_->hide();
    controls_outer_layout->addWidget(status_label_);

    auto* controls_row = new QWidget(controls_frame_);
    controls_row->setObjectName(QStringLiteral("figureControlsRow"));
    controls_layout_ = new QGridLayout(controls_row);
    controls_layout_->setContentsMargins(10, 6, 10, 6);
    controls_layout_->setHorizontalSpacing(14);
    controls_layout_->setVerticalSpacing(9);
    controls_layout_->setColumnStretch(1, 1);

    amplitude_label_ = new QLabel(controls_row);
    amplitude_label_->setObjectName(QStringLiteral("figureAmplitudeLabel"));
    amplitude_slider_ = new QSlider(Qt::Horizontal, controls_row);
    amplitude_slider_->setObjectName(QStringLiteral("figureAmplitudeSlider"));
    amplitude_slider_->setRange(0, kSliderSteps);
    amplitude_slider_->setSingleStep(1);
    amplitude_slider_->setPageStep(10);
    amplitude_slider_->setTickPosition(QSlider::NoTicks);
    frequency_label_ = new QLabel(controls_row);
    frequency_label_->setObjectName(QStringLiteral("figureFrequencyLabel"));
    frequency_slider_ = new QSlider(Qt::Horizontal, controls_row);
    frequency_slider_->setObjectName(QStringLiteral("figureFrequencySlider"));
    frequency_slider_->setRange(0, kSliderSteps);
    animation_button_ = new QPushButton(controls_row);
    animation_button_->setObjectName(QStringLiteral("figureAnimationButton"));
    animation_button_->setCheckable(true);
    reset_button_ = new QPushButton(QStringLiteral("Reset"), controls_row);
    reset_button_->setObjectName(QStringLiteral("figureResetButton"));
    controls_layout_->addWidget(amplitude_label_, 0, 0);
    controls_layout_->addWidget(amplitude_slider_, 0, 1);
    controls_layout_->addWidget(animation_button_, 0, 2);
    controls_layout_->addWidget(frequency_label_, 1, 0);
    controls_layout_->addWidget(frequency_slider_, 1, 1);
    controls_layout_->addWidget(reset_button_, 1, 2);
    controls_outer_layout->addWidget(controls_row);
    root_layout_->addWidget(controls_frame_);

    animation_timer_ = new QTimer(this);
    animation_timer_->setObjectName(QStringLiteral("figureAnimationTimer"));
    animation_timer_->setInterval(kAnimationIntervalMs);
    connect(animation_timer_, &QTimer::timeout, this, &InteractiveFigureWidget::advance_animation);
    connect(amplitude_slider_, &QSlider::valueChanged, this, [this](int position) {
        if (is_box_basis_figure(definition_.kind)) {
            canvas_->set_basis_count(position);
            update_box_basis_status();
        } else if (is_harmonic_time_figure(definition_.kind)) {
            canvas_->set_stretch(slider_value(
                position, definition_.stretch_min, definition_.stretch_max));
            update_harmonic_status();
        } else {
            canvas_->set_amplitude(slider_value(
                position, definition_.amplitude_min, definition_.amplitude_max));
        }
        update_labels();
    });
    connect(frequency_slider_, &QSlider::valueChanged, this, [this](int position) {
        if (is_harmonic_time_figure(definition_.kind)) {
            set_harmonic_phase(slider_value(
                position, definition_.phase_min, definition_.phase_max), false);
            if (animation_requested_) {
                playback_start_phase_ = harmonic_phase_;
                playback_elapsed_.restart();
            } else {
                animation_button_->setText(!definition_.loop
                        && harmonic_phase_ >= definition_.phase_max
                    ? QStringLiteral("Replay") : QStringLiteral("Play"));
            }
        } else {
            canvas_->set_frequency(slider_value(
                position, definition_.frequency_min, definition_.frequency_max));
        }
        update_labels();
    });
    connect(animation_button_, &QPushButton::toggled, this, &InteractiveFigureWidget::set_animation_running);
    connect(reset_button_, &QPushButton::clicked, this, &InteractiveFigureWidget::reset_controls);
}

void InteractiveFigureWidget::set_definition(const InteractiveFigureDefinition& definition) {
    definition_ = definition;
    canvas_->set_definition(definition);
    root_layout_->removeWidget(controls_frame_);
    animation_timer_->stop();
    amplitude_slider_->setRange(0, kSliderSteps);
    frequency_label_->show();
    frequency_slider_->show();
    animation_button_->show();
    controls_layout_->removeWidget(reset_button_);
    controls_layout_->addWidget(
        reset_button_,
        is_box_basis_figure(definition.kind) ? 0 : 1,
        2);
    if (is_harmonic_time_figure(definition.kind)) {
        root_layout_->insertWidget(0, controls_frame_);
        status_label_->show();
        reset_button_->hide();
    } else if (is_box_basis_figure(definition.kind)) {
        root_layout_->addWidget(controls_frame_);
        amplitude_slider_->setRange(definition.basis_count_min, definition.basis_count_max);
        amplitude_slider_->setPageStep(1);
        amplitude_slider_->setTickInterval(1);
        amplitude_slider_->setTickPosition(QSlider::TicksBelow);
        frequency_label_->hide();
        frequency_slider_->hide();
        animation_button_->hide();
        status_label_->show();
        reset_button_->show();
    } else {
        root_layout_->addWidget(controls_frame_);
        status_label_->hide();
        reset_button_->show();
    }
    reset_controls();
}

void InteractiveFigureWidget::set_context_menu_handler(std::function<void(const QPoint&)> handler) {
    context_menu_handler_ = std::move(handler);
}

void InteractiveFigureWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (context_menu_handler_) {
        context_menu_handler_(event->globalPos());
    }
    event->accept();
}

void InteractiveFigureWidget::hideEvent(QHideEvent* event) {
    if (is_harmonic_time_figure(definition_.kind)
        && animation_requested_ && playback_elapsed_.isValid()) {
        const HarmonicPlaybackSample sample = harmonic_playback_sample(
            playback_start_phase_,
            playback_elapsed_.elapsed() / 1000.0,
            definition_.period_seconds,
            definition_.loop);
        set_harmonic_phase(sample.phase, true);
        playback_start_phase_ = harmonic_phase_;
        playback_elapsed_.invalidate();
    }
    animation_timer_->stop();
    QWidget::hideEvent(event);
}

void InteractiveFigureWidget::showEvent(QShowEvent* event) {
    if (animation_requested_) {
        if (is_harmonic_time_figure(definition_.kind)) {
            playback_start_phase_ = harmonic_phase_;
            playback_elapsed_.restart();
        }
        animation_timer_->start();
    }
    QWidget::showEvent(event);
}

void InteractiveFigureWidget::reset_controls() {
    if (is_box_basis_figure(definition_.kind)) {
        animation_requested_ = false;
        animation_timer_->stop();
        {
            const QSignalBlocker blocker(animation_button_);
            animation_button_->setChecked(false);
        }
        amplitude_slider_->setValue(definition_.basis_count_initial);
        canvas_->set_basis_count(definition_.basis_count_initial);
        update_labels();
        update_box_basis_status();
        return;
    }
    if (is_harmonic_time_figure(definition_.kind)) {
        amplitude_slider_->setValue(slider_position(
            definition_.stretch_initial, definition_.stretch_min, definition_.stretch_max));
        set_harmonic_phase(definition_.phase_initial, true);
        animation_button_->setChecked(definition_.animate_initially);
        set_animation_running(definition_.animate_initially);
        update_labels();
        update_harmonic_status();
        return;
    }
    amplitude_slider_->setValue(slider_position(
        definition_.amplitude_initial, definition_.amplitude_min, definition_.amplitude_max));
    frequency_slider_->setValue(slider_position(
        definition_.frequency_initial, definition_.frequency_min, definition_.frequency_max));
    canvas_->reset_phase();
    animation_button_->setChecked(definition_.animate_initially);
    set_animation_running(definition_.animate_initially);
    update_labels();
}

void InteractiveFigureWidget::update_labels() {
    if (is_box_basis_figure(definition_.kind)) {
        amplitude_label_->setText(QStringLiteral("Basis functions <i>N</i>: %1")
            .arg(amplitude_slider_->value()));
        return;
    }
    if (is_harmonic_time_figure(definition_.kind)) {
        amplitude_label_->setText(QStringLiteral("Initial stretch A: %1").arg(
            slider_value(amplitude_slider_->value(), definition_.stretch_min, definition_.stretch_max),
            0, 'f', 2));
        frequency_label_->setText(QStringLiteral("Phase τ: %1 T").arg(
            harmonic_phase_, 0, 'f', 3));
        return;
    }
    amplitude_label_->setText(QStringLiteral("Amplitude: %1").arg(
        slider_value(amplitude_slider_->value(), definition_.amplitude_min, definition_.amplitude_max), 0, 'f', 2));
    frequency_label_->setText(QStringLiteral("Frequency: %1").arg(
        slider_value(frequency_slider_->value(), definition_.frequency_min, definition_.frequency_max), 0, 'f', 2));
}

void InteractiveFigureWidget::update_box_basis_status() {
    if (!is_box_basis_figure(definition_.kind)) {
        return;
    }
    const int basis_count = amplitude_slider_->value();
    const double percentage = 100.0 * particle_in_box_step_captured_fraction(
        basis_count, definition_.step_position, definition_.step_height);
    status_label_->setText(
        QStringLiteral(
            "<i>N</i> = %1 &emsp; &Sigma;<sub><i>n</i>=1</sub><sup><i>N</i></sup> "
            "<i>a</i><sub><i>n</i></sub><sup>2</sup> = %2% &emsp; "
            "— fraction of <i>f</i>(<i>x</i>) reproduced")
            .arg(basis_count)
            .arg(percentage, 0, 'f', 1));
}

void InteractiveFigureWidget::update_harmonic_status() {
    if (!is_harmonic_time_figure(definition_.kind)) {
        return;
    }
    const double stretch = slider_value(
        amplitude_slider_->value(), definition_.stretch_min, definition_.stretch_max);
    if (definition_.kind == InteractiveFigureDefinition::Kind::HarmonicBasisStates) {
        double displayed_weight = 0.0;
        for (int n = 0; n < 6; ++n) {
            displayed_weight += coherent_state_basis_weight(n, stretch);
        }
        status_label_->setText(
            QStringLiteral("τ = %1 <i>T</i> &emsp; "
                           "&Sigma;<sub>n=0</sub><sup>5</sup> "
                           "|<i>c</i><sub>n</sub>|<sup>2</sup> = %2%")
                .arg(harmonic_phase_, 0, 'f', 3)
                .arg(100.0 * displayed_weight, 0, 'f', 1));
        return;
    }
    const HarmonicWavepacketObservation observation = evaluate_harmonic_wavepacket(
        harmonic_phase_, stretch);
    status_label_->setText(QStringLiteral("X = %1 ℓ     P = %2 ħ/ℓ     — %3")
        .arg(observation.mean_position, 0, 'f', 2)
        .arg(observation.mean_momentum, 0, 'f', 2)
        .arg(observation.stage));
}

void InteractiveFigureWidget::advance_animation() {
    if (!is_harmonic_time_figure(definition_.kind)) {
        canvas_->advance_phase();
        return;
    }
    if (!playback_elapsed_.isValid()) {
        playback_start_phase_ = harmonic_phase_;
        playback_elapsed_.start();
    }
    const HarmonicPlaybackSample sample = harmonic_playback_sample(
        playback_start_phase_,
        playback_elapsed_.elapsed() / 1000.0,
        definition_.period_seconds,
        definition_.loop);
    set_harmonic_phase(sample.phase, true);
    if (!sample.running) {
        animation_button_->setChecked(false);
    }
}

void InteractiveFigureWidget::set_harmonic_phase(double phase, bool update_slider) {
    harmonic_phase_ = std::clamp(phase, definition_.phase_min, definition_.phase_max);
    canvas_->set_harmonic_phase(harmonic_phase_);
    if (update_slider) {
        const QSignalBlocker blocker(frequency_slider_);
        frequency_slider_->setValue(slider_position(
            harmonic_phase_, definition_.phase_min, definition_.phase_max));
    }
    update_labels();
    update_harmonic_status();
}

void InteractiveFigureWidget::set_animation_running(bool running) {
    animation_requested_ = running;
    if (is_harmonic_time_figure(definition_.kind)) {
        if (!running && playback_elapsed_.isValid()) {
            const HarmonicPlaybackSample sample = harmonic_playback_sample(
                playback_start_phase_,
                playback_elapsed_.elapsed() / 1000.0,
                definition_.period_seconds,
                definition_.loop);
            set_harmonic_phase(sample.phase, true);
        }
        if (running && harmonic_phase_ >= definition_.phase_max) {
            set_harmonic_phase(definition_.phase_min, true);
        }
        if (running) {
            playback_start_phase_ = harmonic_phase_;
            playback_elapsed_.restart();
        } else {
            playback_elapsed_.invalidate();
        }
        animation_button_->setText(running
            ? QStringLiteral("Pause")
            : (!definition_.loop && harmonic_phase_ >= definition_.phase_max
                ? QStringLiteral("Replay") : QStringLiteral("Play")));
    } else {
        animation_button_->setText(running ? QStringLiteral("Pause") : QStringLiteral("Play"));
    }
    if (running && isVisible()) {
        animation_timer_->start();
    } else {
        animation_timer_->stop();
    }
}
