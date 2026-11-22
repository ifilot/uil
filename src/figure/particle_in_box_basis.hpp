#pragma once

#include <QPointF>
#include <QVector>

/*
 * The target function is a step on the normalized box [0, 1]: 0 for x < step_position and
 * step_height for x >= step_position. It is expanded in the box eigenfunctions sin(n pi x).
 */

/**
 * @brief Sine-series coefficient for a step on the normalized box [0, 1].
 * @param basis_index One-based index n of sin(n pi x).
 * @return The coefficient, or 0 for n < 1, a step position outside (0, 1), or a non-finite
 *         height.
 */
double particle_in_box_step_coefficient(int basis_index, double step_position = 0.5,
                                        double step_height = 1.0);

/** @brief Evaluates the first @p basis_count terms of the step's sine expansion at @p x. */
double particle_in_box_step_approximation(double x, int basis_count, double step_position = 0.5,
                                          double step_height = 1.0);

/**
 * @brief Fraction of the target function's squared norm captured by the partial sum.
 * @return A value in [0, 1]; 0 for invalid parameters or a zero step height.
 */
double particle_in_box_step_captured_fraction(int basis_count, double step_position = 0.5,
                                              double step_height = 1.0);

/**
 * @brief Samples a partial sine-series sum across the normalized box.
 * @return @p sample_count points from x = 0 to x = 1, or none when @p basis_count < 1 or
 *         @p sample_count < 2.
 */
QVector<QPointF> sample_particle_in_box_step_approximation(int basis_count, int sample_count,
                                                           double step_position = 0.5,
                                                           double step_height = 1.0);
