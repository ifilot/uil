#include "figure/particle_in_box_basis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

double particle_in_box_step_coefficient(
    int basis_index,
    double step_position,
    double step_height) {
    if (basis_index < 1 || !(step_position > 0.0 && step_position < 1.0)
        || !std::isfinite(step_height)) {
        return 0.0;
    }
    const double n_pi = basis_index * std::numbers::pi;
    return 2.0 * step_height
        * (std::cos(n_pi * step_position) - std::cos(n_pi)) / n_pi;
}

double particle_in_box_step_approximation(
    double x,
    int basis_count,
    double step_position,
    double step_height) {
    if (!std::isfinite(x) || basis_count < 1) {
        return 0.0;
    }
    double value = 0.0;
    for (int n = 1; n <= basis_count; ++n) {
        value += particle_in_box_step_coefficient(n, step_position, step_height)
            * std::sin(n * std::numbers::pi * x);
    }
    return value;
}

double particle_in_box_step_captured_fraction(
    int basis_count,
    double step_position,
    double step_height) {
    if (basis_count < 1 || !(step_position > 0.0 && step_position < 1.0)
        || !std::isfinite(step_height) || std::abs(step_height) < 1e-15) {
        return 0.0;
    }
    double coefficient_square_sum = 0.0;
    for (int n = 1; n <= basis_count; ++n) {
        const double coefficient = particle_in_box_step_coefficient(
            n, step_position, step_height);
        coefficient_square_sum += coefficient * coefficient;
    }
    const double target_squared_norm = step_height * step_height * (1.0 - step_position);
    return std::clamp(0.5 * coefficient_square_sum / target_squared_norm, 0.0, 1.0);
}

QVector<QPointF> sample_particle_in_box_step_approximation(
    int basis_count,
    int sample_count,
    double step_position,
    double step_height) {
    if (basis_count < 1 || sample_count < 2) {
        return {};
    }
    QVector<QPointF> samples;
    samples.reserve(sample_count);
    for (int i = 0; i < sample_count; ++i) {
        const double x = double(i) / double(sample_count - 1);
        samples.push_back(QPointF(
            x,
            particle_in_box_step_approximation(
                x, basis_count, step_position, step_height)));
    }
    return samples;
}
