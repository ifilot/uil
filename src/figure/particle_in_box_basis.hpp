#pragma once

#include <QPointF>
#include <QVector>

/** @brief Sine-series coefficient for a step on the normalized box [0, 1]. */
double particle_in_box_step_coefficient(
    int basis_index,
    double step_position = 0.5,
    double step_height = 1.0);

/** @brief Evaluates the first basis_count terms of the step's sine expansion. */
double particle_in_box_step_approximation(
    double x,
    int basis_count,
    double step_position = 0.5,
    double step_height = 1.0);

/** @brief Fraction of the target function's squared norm captured by the partial sum. */
double particle_in_box_step_captured_fraction(
    int basis_count,
    double step_position = 0.5,
    double step_height = 1.0);

/** @brief Samples a partial sine-series sum across the normalized box. */
QVector<QPointF> sample_particle_in_box_step_approximation(
    int basis_count,
    int sample_count,
    double step_position = 0.5,
    double step_height = 1.0);
