#include "ui/interactive_figure_widget.hpp"

#include "figure/harmonic_wavepacket.hpp"

#include <QContextMenuEvent>
#include <QFrame>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSvgRenderer>
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
        phase_ = 0.0;
        update();
    }

    void set_amplitude(double value) { amplitude_ = value; update(); }
    void set_frequency(double value) { frequency_ = value; update(); }
    void advance_phase() { phase_ = std::fmod(phase_ + kPhaseStep, 2.0 * std::numbers::pi); update(); }
    void reset_phase() { phase_ = 0.0; update(); }
    void set_harmonic_phase(double value) { harmonic_phase_ = value; update(); }
    void set_stretch(double value) { stretch_ = value; update(); }
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

        QFont title_font = painter.font();
        title_font.setBold(true);
        title_font.setPixelSize(std::clamp(height() / 17, 16, 30));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_height = title_metrics.height() + 10.0;
        const qreal title_top = 5.0;
        const qreal left_margin = std::max(78, width() / 18);
        const qreal right_margin = std::max(34, width() / 40);
        const qreal bottom_margin = std::max(54, height() / 9);
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
        tick_font.setPixelSize(std::clamp(height() / 36, 10, 15));
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
        axis_font.setPixelSize(std::clamp(height() / 30, 11, 17));
        painter.setFont(axis_font);
        painter.setPen(QColor(30, 41, 59));
        painter.drawText(
            QRectF(plot_rect.left(), height() - 25.0, plot_rect.width(), 20.0),
            Qt::AlignCenter,
            definition_.x_label);
        painter.save();
        painter.translate(15.0, plot_rect.center().y());
        painter.rotate(-90.0);
        painter.drawText(
            QRectF(-plot_rect.height() / 2.0, -10.0, plot_rect.height(), 20.0),
            Qt::AlignCenter,
            definition_.y_label);
        painter.restore();
    }

private:
    void paint_harmonic_wavepacket(QPainter& painter) {
        const HarmonicWavepacketObservation observation = evaluate_harmonic_wavepacket(
            harmonic_phase_, stretch_);

        QFont title_font = painter.font();
        title_font.setBold(true);
        title_font.setPixelSize(std::clamp(height() / 25, 15, 25));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_top = 3.0;
        const qreal title_height = title_metrics.height() + 6.0;
        painter.setFont(title_font);
        painter.setPen(QColor(15, 23, 42));
        painter.drawText(
            QRectF(16.0, title_top, width() - 32.0, title_height),
            Qt::AlignCenter,
            title_metrics.elidedText(definition_.title, Qt::ElideRight, width() - 32));

        const qreal left_margin = std::max(92, width() / 14);
        const qreal right_margin = std::max(34, width() / 35);
        const qreal bottom_margin = std::max(38, height() / 13);
        const qreal plot_gap = std::max(36, height() / 15);
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
        text_font.setPixelSize(std::clamp(height() / 48, 9, 13));
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
        label_font.setPixelSize(std::clamp(height() / 42, 10, 14));
        painter.setFont(label_font);
        painter.setPen(QColor(30, 41, 59));
        painter.drawText(QRectF(lower.left(), height() - 24.0, lower.width(), 20.0),
                         Qt::AlignCenter, definition_.x_label);
        const auto draw_vertical_label = [&](const QRectF& area, const QString& label) {
            painter.save();
            painter.translate(15.0, area.center().y());
            painter.rotate(-90.0);
            painter.drawText(QRectF(-area.height() / 2.0, -10.0, area.height(), 20.0),
                             Qt::AlignCenter, label);
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
        title_font.setPixelSize(std::clamp(height() / 25, 15, 25));
        const QFontMetricsF title_metrics(title_font);
        const qreal title_top = 3.0;
        const qreal title_height = title_metrics.height() + 6.0;
        painter.setFont(title_font);
        painter.setPen(QColor(15, 23, 42));
        painter.drawText(QRectF(16.0, title_top, width() - 32.0, title_height),
                         Qt::AlignCenter,
                         title_metrics.elidedText(definition_.title, Qt::ElideRight, width() - 32));

        const qreal left_margin = std::max(62, width() / 18);
        const qreal right_margin = std::max(20, width() / 45);
        const qreal bottom_margin = std::max(28, height() / 18);
        const qreal row_gap = std::max(10, height() / 55);
        const qreal column_gap = std::max(14, width() / 65);
        const qreal top = title_top + title_height + 3.0;
        const qreal available = height() - top - bottom_margin;
        if (available < 210.0) {
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
        text_font.setPixelSize(std::clamp(height() / 58, 8, 11));
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
        painter.setFont(text_font);
        painter.setPen(QColor(30, 41, 59));
        painter.drawText(
            QRectF(coherent_panel.left() + 6.0, coherent_panel.top() + 2.0,
                   coherent_panel.width() - 12.0, metrics.height()),
            Qt::AlignLeft | Qt::AlignTop,
            QStringLiteral("Coherent-state density   X = %1 ℓ")
                .arg(observation.mean_position, 0, 'f', 2));

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
            const double phase_pi = -2.0 * n * harmonic_phase_;
            painter.setFont(text_font);
            painter.setPen(QColor(30, 41, 59));
            painter.drawText(
                QRectF(panel.left() + 7.0, panel.top() + 2.0,
                       panel.width() - 14.0, metrics.height()),
                Qt::AlignLeft | Qt::AlignTop,
                QStringLiteral("n=%1   |cₙ|²=%2%   φₙ=%3π")
                    .arg(n)
                    .arg(100.0 * weight, 0, 'f', 1)
                    .arg(phase_pi, 0, 'f', 2));
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
        label_font.setPixelSize(std::clamp(height() / 45, 9, 13));
        painter.setFont(label_font);
        painter.setPen(QColor(30, 41, 59));
        painter.drawText(QRectF(left_margin, height() - 21.0,
                                full_width, 18.0),
                         Qt::AlignCenter, definition_.x_label);
        painter.save();
        painter.translate(14.0, grid_top + component_height + row_gap / 2.0);
        painter.rotate(-90.0);
        painter.drawText(QRectF(-(2.0 * component_height + row_gap) / 2.0, -10.0,
                                2.0 * component_height + row_gap, 20.0),
                         Qt::AlignCenter, QStringLiteral("Re[cₙ ψₙ(x) e⁻ⁱⁿωᵗ]"));
        painter.restore();
    }

    InteractiveFigureDefinition definition_;
    QSvgRenderer renderer_;
    double amplitude_ = 1.0;
    double frequency_ = 1.0;
    double phase_ = 0.0;
    double harmonic_phase_ = 0.0;
    double stretch_ = 3.0;
};

InteractiveFigureWidget::InteractiveFigureWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("interactiveFigureWidget"));
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "InteractiveFigureWidget { background: white; border: 1px solid #94a3b8; }"
        "QFrame#figureControls { background: #f1f5f9; border-top: 1px solid #cbd5e1; }"
        "QLabel#figureAmplitudeLabel, QLabel#figureFrequencyLabel {"
        " background: transparent; color: #1e293b; border: none; padding: 0 3px;"
        " min-width: 108px; }"
        "QSlider#figureAmplitudeSlider, QSlider#figureFrequencySlider {"
        " background: transparent; }"
        "QSlider#figureAmplitudeSlider::groove:horizontal,"
        "QSlider#figureFrequencySlider::groove:horizontal {"
        " height: 5px; background: #cbd5e1; border-radius: 2px; }"
        "QSlider#figureAmplitudeSlider::sub-page:horizontal,"
        "QSlider#figureFrequencySlider::sub-page:horizontal {"
        " background: #0891b2; border-radius: 2px; }"
        "QSlider#figureAmplitudeSlider::handle:horizontal,"
        "QSlider#figureFrequencySlider::handle:horizontal {"
        " width: 14px; margin: -5px 0; background: #0e7490;"
        " border: 1px solid #155e75; border-radius: 7px; }"
        "QLabel#figureStatusLabel { background: transparent; color: #334155;"
        " border: none; padding: 2px 4px; font-weight: 600; }"
        "QWidget#figureControlsRow { background: transparent; }"
        "QPushButton#figureAnimationButton, QPushButton#figureResetButton {"
        " background: #0f766e; color: white; border: 1px solid #0f766e;"
        " padding: 5px 12px; min-width: 72px; }"
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
    controls_outer_layout->setContentsMargins(10, 4, 10, 6);
    controls_outer_layout->setSpacing(2);
    status_label_ = new QLabel(controls_frame_);
    status_label_->setObjectName(QStringLiteral("figureStatusLabel"));
    status_label_->setAlignment(Qt::AlignCenter);
    status_label_->hide();
    controls_outer_layout->addWidget(status_label_);

    auto* controls_row = new QWidget(controls_frame_);
    controls_row->setObjectName(QStringLiteral("figureControlsRow"));
    auto* controls_layout = new QHBoxLayout(controls_row);
    controls_layout->setContentsMargins(10, 6, 10, 6);
    controls_layout->setSpacing(8);

    amplitude_label_ = new QLabel(controls_row);
    amplitude_label_->setObjectName(QStringLiteral("figureAmplitudeLabel"));
    amplitude_slider_ = new QSlider(Qt::Horizontal, controls_row);
    amplitude_slider_->setObjectName(QStringLiteral("figureAmplitudeSlider"));
    amplitude_slider_->setRange(0, kSliderSteps);
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
    controls_layout->addWidget(amplitude_label_);
    controls_layout->addWidget(amplitude_slider_, 1);
    controls_layout->addWidget(frequency_label_);
    controls_layout->addWidget(frequency_slider_, 1);
    controls_layout->addWidget(animation_button_);
    controls_layout->addWidget(reset_button_);
    controls_outer_layout->addWidget(controls_row);
    root_layout_->addWidget(controls_frame_);

    animation_timer_ = new QTimer(this);
    animation_timer_->setObjectName(QStringLiteral("figureAnimationTimer"));
    animation_timer_->setInterval(kAnimationIntervalMs);
    connect(animation_timer_, &QTimer::timeout, this, &InteractiveFigureWidget::advance_animation);
    connect(amplitude_slider_, &QSlider::valueChanged, this, [this](int position) {
        if (is_harmonic_time_figure(definition_.kind)) {
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
    if (is_harmonic_time_figure(definition.kind)) {
        root_layout_->insertWidget(0, controls_frame_);
        status_label_->show();
        reset_button_->hide();
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
            QStringLiteral("τ = %1 T     Σ₀⁵ |cₙ|² = %2%     — coherent density moves; real components evolve")
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
