#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QVector>

/** @brief Version-one definition for a small, self-contained interactive plot. */
struct InteractiveFigureDefinition {
    enum class Kind {
        SineWave,
        HarmonicBondWavepacket,
        HarmonicBasisStates,
        ParticleInBoxStepExpansion,
        HarmonicDisplacedStateExpansion,
    };

    Kind kind = Kind::SineWave;
    QString title;
    QString x_label = QStringLiteral("x");
    QString y_label = QStringLiteral("y");
    QByteArray background_svg;
    QColor curve_color = QColor(QStringLiteral("#2563eb"));
    double x_min = -6.283185307179586;
    double x_max = 6.283185307179586;
    double y_min = -2.5;
    double y_max = 2.5;
    double amplitude_min = 0.0;
    double amplitude_max = 2.0;
    double amplitude_initial = 1.0;
    double frequency_min = 0.25;
    double frequency_max = 3.0;
    double frequency_initial = 1.0;
    bool animate_initially = true;

    QString potential_label = QStringLiteral("U / (ħω)");
    QString density_label = QStringLiteral("ℓ|ψ|²");
    QColor potential_color = QColor(QStringLiteral("#475569"));
    QColor density_color = QColor(QStringLiteral("#0f766e"));
    QColor marker_color = QColor(QStringLiteral("#dc2626"));
    double potential_y_max = 12.5;
    double density_y_max = 0.65;
    double phase_min = 0.0;
    double phase_max = 1.0;
    double phase_initial = 0.0;
    double stretch_min = 0.5;
    double stretch_max = 4.0;
    double stretch_initial = 3.0;
    double period_seconds = 8.0;
    bool loop = false;
    bool show_energy_reference = true;
    QVector<QColor> basis_colors = {
        QColor(QStringLiteral("#2563eb")),
        QColor(QStringLiteral("#0f766e")),
        QColor(QStringLiteral("#b45309")),
        QColor(QStringLiteral("#7c3aed")),
        QColor(QStringLiteral("#be185d")),
        QColor(QStringLiteral("#0369a1")),
    };

    double step_position = 0.5;
    double step_height = 1.0;
    double displacement = 2.0;
    int basis_count_min = 1;
    int basis_count_max = 25;
    int basis_count_initial = 1;
    QColor target_color = QColor(QStringLiteral("#64748b"));
    QColor approximation_color = QColor(QStringLiteral("#0f766e"));

    /** @brief Returns whether the definition contains usable plot bounds and SVG artwork. */
    bool is_valid() const;
};

/** @brief Parses and validates a version-one UIL interactive-figure JSON payload. */
bool parse_interactive_figure(
    const QByteArray& payload,
    InteractiveFigureDefinition* definition,
    QString* error_message = nullptr);
