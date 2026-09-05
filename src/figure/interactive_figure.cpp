#include "figure/interactive_figure.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>

namespace {
constexpr qsizetype kMaximumFigureBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumSvgBytes = 2 * 1024 * 1024;

void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

double finite_number(const QJsonObject& object, const QString& key, double fallback) {
    const double value = object.value(key).toDouble(fallback);
    return std::isfinite(value) ? value : fallback;
}
}

bool InteractiveFigureDefinition::is_valid() const {
    if (background_svg.isEmpty() || !(x_max > x_min)) {
        return false;
    }
    if (kind == Kind::ParticleInBoxStepExpansion) {
        return std::abs(x_min) < 1e-12
            && std::abs(x_max - 1.0) < 1e-12
            && y_max > y_min
            && step_position > 0.0
            && step_position < 1.0
            && std::isfinite(step_height)
            && step_height > 0.0
            && basis_count_min >= 1
            && basis_count_max >= basis_count_min
            && basis_count_max <= 200
            && basis_count_initial >= basis_count_min
            && basis_count_initial <= basis_count_max;
    }
    if (kind == Kind::HarmonicBondWavepacket || kind == Kind::HarmonicBasisStates) {
        return potential_y_max > 0.0
            && density_y_max > 0.0
            && std::abs(phase_min) < 1e-12
            && std::abs(phase_max - 1.0) < 1e-12
            && phase_initial >= phase_min
            && phase_initial <= phase_max
            && stretch_max > stretch_min
            && stretch_initial >= stretch_min
            && stretch_initial <= stretch_max
            && period_seconds > 0.0;
    }
    return y_max > y_min
        && amplitude_max > amplitude_min
        && frequency_max > frequency_min
        && amplitude_initial >= amplitude_min
        && amplitude_initial <= amplitude_max
        && frequency_initial >= frequency_min
        && frequency_initial <= frequency_max;
}

bool parse_interactive_figure(
    const QByteArray& payload,
    InteractiveFigureDefinition* definition,
    QString* error_message) {
    if (!definition) {
        set_error(error_message, QStringLiteral("Missing interactive-figure output"));
        return false;
    }
    if (payload.isEmpty() || payload.size() > kMaximumFigureBytes) {
        set_error(error_message, QStringLiteral("Interactive-figure payload is empty or exceeds 4 MiB"));
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        set_error(error_message,
            QStringLiteral("Interactive-figure JSON is invalid: %1").arg(parse_error.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("uil.interactive-figure")
        || root.value(QStringLiteral("version")).toInt(-1) != 1) {
        set_error(error_message, QStringLiteral("Unsupported interactive-figure format or version"));
        return false;
    }

    const QJsonObject plot = root.value(QStringLiteral("plot")).toObject();
    const QString kind = plot.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("sine-wave")
        && kind != QStringLiteral("harmonic-bond-wavepacket")
        && kind != QStringLiteral("harmonic-basis-states")
        && kind != QStringLiteral("particle-in-box-step-expansion")) {
        set_error(error_message, QStringLiteral("Unsupported interactive-figure plot kind"));
        return false;
    }

    InteractiveFigureDefinition parsed;
    if (kind == QStringLiteral("harmonic-bond-wavepacket")) {
        parsed.kind = InteractiveFigureDefinition::Kind::HarmonicBondWavepacket;
    } else if (kind == QStringLiteral("harmonic-basis-states")) {
        parsed.kind = InteractiveFigureDefinition::Kind::HarmonicBasisStates;
    } else if (kind == QStringLiteral("particle-in-box-step-expansion")) {
        parsed.kind = InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion;
    } else {
        parsed.kind = InteractiveFigureDefinition::Kind::SineWave;
    }
    parsed.title = root.value(QStringLiteral("title")).toString(QStringLiteral("Interactive figure"));
    parsed.background_svg = root.value(QStringLiteral("background_svg")).toString().toUtf8();
    if (parsed.background_svg.size() > kMaximumSvgBytes) {
        set_error(error_message, QStringLiteral("Interactive-figure SVG exceeds 2 MiB"));
        return false;
    }

    parsed.x_min = finite_number(plot, QStringLiteral("x_min"), parsed.x_min);
    parsed.x_max = finite_number(plot, QStringLiteral("x_max"), parsed.x_max);
    parsed.x_label = plot.value(QStringLiteral("x_label")).toString(parsed.x_label);

    const QJsonObject controls = root.value(QStringLiteral("controls")).toObject();
    if (parsed.kind == InteractiveFigureDefinition::Kind::HarmonicBondWavepacket
        || parsed.kind == InteractiveFigureDefinition::Kind::HarmonicBasisStates) {
        parsed.potential_y_max = finite_number(
            plot, QStringLiteral("potential_y_max"), parsed.potential_y_max);
        parsed.density_y_max = finite_number(
            plot, QStringLiteral("density_y_max"), parsed.density_y_max);
        parsed.potential_label = plot.value(QStringLiteral("potential_label")).toString(
            parsed.potential_label);
        parsed.density_label = plot.value(QStringLiteral("density_label")).toString(
            parsed.density_label);
        const QColor potential_color(plot.value(QStringLiteral("potential_color")).toString());
        const QColor density_color(plot.value(QStringLiteral("density_color")).toString());
        const QColor marker_color(plot.value(QStringLiteral("marker_color")).toString());
        if (potential_color.isValid()) parsed.potential_color = potential_color;
        if (density_color.isValid()) parsed.density_color = density_color;
        if (marker_color.isValid()) parsed.marker_color = marker_color;
        const QJsonArray basis_colors = plot.value(QStringLiteral("basis_colors")).toArray();
        if (basis_colors.size() == 6) {
            QVector<QColor> parsed_colors;
            for (const QJsonValue& value : basis_colors) {
                const QColor color(value.toString());
                if (color.isValid()) parsed_colors.push_back(color);
            }
            if (parsed_colors.size() == 6) parsed.basis_colors = std::move(parsed_colors);
        }
        parsed.show_energy_reference = plot.value(
            QStringLiteral("show_energy_reference")).toBool(true);

        const QJsonObject phase = controls.value(QStringLiteral("phase")).toObject();
        parsed.phase_min = finite_number(phase, QStringLiteral("min"), parsed.phase_min);
        parsed.phase_max = finite_number(phase, QStringLiteral("max"), parsed.phase_max);
        parsed.phase_initial = finite_number(phase, QStringLiteral("value"), parsed.phase_initial);
        const QJsonObject stretch = controls.value(QStringLiteral("initial_stretch")).toObject();
        parsed.stretch_min = finite_number(stretch, QStringLiteral("min"), parsed.stretch_min);
        parsed.stretch_max = finite_number(stretch, QStringLiteral("max"), parsed.stretch_max);
        parsed.stretch_initial = finite_number(stretch, QStringLiteral("value"), parsed.stretch_initial);
        parsed.period_seconds = finite_number(
            controls, QStringLiteral("period_seconds"), parsed.period_seconds);
        parsed.loop = controls.value(QStringLiteral("loop")).toBool(false);
    } else if (parsed.kind == InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion) {
        parsed.y_min = finite_number(plot, QStringLiteral("y_min"), parsed.y_min);
        parsed.y_max = finite_number(plot, QStringLiteral("y_max"), parsed.y_max);
        parsed.y_label = plot.value(QStringLiteral("y_label")).toString(parsed.y_label);
        parsed.step_position = finite_number(
            plot, QStringLiteral("step_position"), parsed.step_position);
        parsed.step_height = finite_number(
            plot, QStringLiteral("step_height"), parsed.step_height);
        const QColor target_color(plot.value(QStringLiteral("target_color")).toString());
        const QColor approximation_color(
            plot.value(QStringLiteral("approximation_color")).toString());
        if (target_color.isValid()) parsed.target_color = target_color;
        if (approximation_color.isValid()) {
            parsed.approximation_color = approximation_color;
        }

        const QJsonObject basis_count = controls.value(
            QStringLiteral("basis_count")).toObject();
        parsed.basis_count_min = basis_count.value(QStringLiteral("min")).toInt(
            parsed.basis_count_min);
        parsed.basis_count_max = basis_count.value(QStringLiteral("max")).toInt(
            parsed.basis_count_max);
        parsed.basis_count_initial = basis_count.value(QStringLiteral("value")).toInt(
            parsed.basis_count_initial);
    } else {
        const QColor color(plot.value(QStringLiteral("color")).toString(QStringLiteral("#2563eb")));
        if (color.isValid()) parsed.curve_color = color;
        parsed.y_min = finite_number(plot, QStringLiteral("y_min"), parsed.y_min);
        parsed.y_max = finite_number(plot, QStringLiteral("y_max"), parsed.y_max);
        parsed.y_label = plot.value(QStringLiteral("y_label")).toString(parsed.y_label);
        const QJsonObject amplitude = controls.value(QStringLiteral("amplitude")).toObject();
        parsed.amplitude_min = finite_number(amplitude, QStringLiteral("min"), parsed.amplitude_min);
        parsed.amplitude_max = finite_number(amplitude, QStringLiteral("max"), parsed.amplitude_max);
        parsed.amplitude_initial = finite_number(amplitude, QStringLiteral("value"), parsed.amplitude_initial);
        const QJsonObject frequency = controls.value(QStringLiteral("frequency")).toObject();
        parsed.frequency_min = finite_number(frequency, QStringLiteral("min"), parsed.frequency_min);
        parsed.frequency_max = finite_number(frequency, QStringLiteral("max"), parsed.frequency_max);
        parsed.frequency_initial = finite_number(frequency, QStringLiteral("value"), parsed.frequency_initial);
    }
    parsed.animate_initially = controls.value(QStringLiteral("animate")).toBool(true);

    if (parsed.title.size() > 200
        || parsed.x_label.size() > 100
        || parsed.y_label.size() > 100
        || parsed.potential_label.size() > 100
        || parsed.density_label.size() > 100) {
        set_error(error_message, QStringLiteral("Interactive-figure text exceeds its size limit"));
        return false;
    }

    if (!parsed.is_valid()) {
        set_error(error_message, QStringLiteral("Interactive-figure ranges or initial values are invalid"));
        return false;
    }

    *definition = std::move(parsed);
    if (error_message) {
        error_message->clear();
    }
    return true;
}
