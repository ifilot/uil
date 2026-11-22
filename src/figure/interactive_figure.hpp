#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QVector>

/**
 * @brief Version-one definition for a small, self-contained interactive plot.
 *
 * Fields absent from the payload keep the defaults below; only the fields relevant to #kind are
 * read (see docs/interactive-figure-v1.schema.json).
 */
struct InteractiveFigureDefinition {
    /** @brief Plot type, selected by the JSON `plot.kind` string. */
    enum class Kind {
        SineWave,
        HarmonicBondWavepacket,
        HarmonicBasisStates,
        ParticleInBoxStepExpansion,
        HarmonicDisplacedStateExpansion,
    };

    /** @brief Plot type; selects which of the field groups below apply. */
    Kind kind = Kind::SineWave;
    // Common fields.
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

    // Harmonic wavepacket and basis-state plots.
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
        QColor(QStringLiteral("#2563eb")), QColor(QStringLiteral("#0f766e")),
        QColor(QStringLiteral("#b45309")), QColor(QStringLiteral("#7c3aed")),
        QColor(QStringLiteral("#be185d")), QColor(QStringLiteral("#0369a1")),
    };

    // Particle-in-box and displaced-state basis expansions.
    double step_position = 0.5;
    double step_height = 1.0;
    double displacement = 2.0;
    int basis_count_min = 1;
    int basis_count_max = 25;
    int basis_count_initial = 1;
    QColor target_color = QColor(QStringLiteral("#64748b"));
    QColor approximation_color = QColor(QStringLiteral("#0f766e"));

    /**
     * @brief Returns whether the definition contains usable plot bounds and SVG artwork.
     *
     * Also checks the ranges and initial values used by #kind, such as min < max, initial
     * values inside their range, and basis counts within [1, 200].
     */
    bool is_valid() const;
};

/**
 * @brief Parses and validates a version-one UIL interactive-figure JSON payload.
 *
 * Requires `format` = `uil.interactive-figure` and `version` = 1, a known plot kind, a payload
 * of at most 4 MiB with at most 2 MiB of background SVG, and bounded label lengths.
 * Non-finite numbers and invalid colors fall back to their defaults.
 *
 * @param definition Receives the parsed definition; untouched on failure.
 * @param error_message Optional; receives the reason on failure and is cleared on success.
 * @return True when the payload is valid.
 */
bool parse_interactive_figure(const QByteArray& payload, InteractiveFigureDefinition* definition,
                              QString* error_message = nullptr);
