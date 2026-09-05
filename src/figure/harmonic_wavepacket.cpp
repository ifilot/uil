#include "figure/harmonic_wavepacket.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
QString stage_description(double phase) {
    constexpr double near_turning_point = 0.035;
    const double wrapped = phase >= 1.0 ? 1.0 : std::clamp(phase, 0.0, 1.0);
    if (wrapped <= near_turning_point || wrapped >= 1.0 - near_turning_point) {
        return QStringLiteral("maximum stretch");
    }
    if (std::abs(wrapped - 0.25) <= near_turning_point) {
        return QStringLiteral("crossing equilibrium inward");
    }
    if (std::abs(wrapped - 0.5) <= near_turning_point) {
        return QStringLiteral("maximum compression");
    }
    if (std::abs(wrapped - 0.75) <= near_turning_point) {
        return QStringLiteral("crossing equilibrium outward");
    }
    if (wrapped < 0.25) {
        return QStringLiteral("moving inward from maximum stretch");
    }
    if (wrapped < 0.5) {
        return QStringLiteral("moving toward maximum compression");
    }
    if (wrapped < 0.75) {
        return QStringLiteral("moving outward from maximum compression");
    }
    return QStringLiteral("returning to maximum stretch");
}
}  // namespace

HarmonicWavepacketObservation evaluate_harmonic_wavepacket(
    double phase,
    double initial_stretch) {
    const double clamped_phase = std::clamp(phase, 0.0, 1.0);
    const double angle = 2.0 * std::numbers::pi * clamped_phase;
    HarmonicWavepacketObservation observation;
    observation.phase = clamped_phase;
    observation.mean_position = initial_stretch * std::cos(angle);
    observation.mean_momentum = -initial_stretch * std::sin(angle);
    observation.potential_at_mean = 0.5 * observation.mean_position * observation.mean_position;
    observation.stage = stage_description(clamped_phase);
    return observation;
}

double harmonic_probability_density(double x, double mean_position) {
    const double offset = x - mean_position;
    return std::exp(-offset * offset) / std::sqrt(std::numbers::pi);
}

QVector<QPointF> sample_harmonic_probability_density(
    double x_min,
    double x_max,
    int sample_count,
    double mean_position) {
    QVector<QPointF> samples;
    if (!(x_max > x_min) || sample_count < 2) {
        return samples;
    }
    samples.reserve(sample_count);
    for (int i = 0; i < sample_count; ++i) {
        const double x = x_min + (x_max - x_min) * i / double(sample_count - 1);
        samples.push_back(QPointF(x, harmonic_probability_density(x, mean_position)));
    }
    return samples;
}

HarmonicPlaybackSample harmonic_playback_sample(
    double starting_phase,
    double elapsed_seconds,
    double period_seconds,
    bool loop) {
    const double clamped_start = std::clamp(starting_phase, 0.0, 1.0);
    if (!(period_seconds > 0.0) || elapsed_seconds < 0.0) {
        return {clamped_start, false};
    }
    if (loop) {
        double phase = std::fmod(clamped_start + elapsed_seconds / period_seconds, 1.0);
        if (phase < 0.0) phase += 1.0;
        return {phase, true};
    }
    if (clamped_start >= 1.0) return {clamped_start, false};
    const double phase = std::min(1.0, clamped_start + elapsed_seconds / period_seconds);
    return {phase, phase < 1.0};
}

double harmonic_basis_probability_density(int n, double x) {
    const double wavefunction = harmonic_basis_wavefunction(n, x);
    return wavefunction * wavefunction;
}

double harmonic_basis_wavefunction(int n, double x) {
    if (n < 0 || n > 32) {
        return 0.0;
    }
    double hermite_previous = 1.0;
    double hermite = n == 0 ? 1.0 : 2.0 * x;
    for (int order = 1; order < n; ++order) {
        const double next = 2.0 * x * hermite - 2.0 * order * hermite_previous;
        hermite_previous = hermite;
        hermite = next;
    }
    double normalization_denominator = 1.0;
    for (int order = 1; order <= n; ++order) {
        normalization_denominator *= 2.0 * order;
    }
    return hermite * std::exp(-0.5 * x * x)
        / (std::pow(std::numbers::pi, 0.25) * std::sqrt(normalization_denominator));
}

double coherent_state_basis_weight(int n, double initial_stretch) {
    if (n < 0) {
        return 0.0;
    }
    const double mean_occupation = 0.5 * initial_stretch * initial_stretch;
    double factorial = 1.0;
    for (int order = 2; order <= n; ++order) {
        factorial *= order;
    }
    return std::exp(-mean_occupation) * std::pow(mean_occupation, n) / factorial;
}

double coherent_state_basis_real_component(
    int n,
    double x,
    double phase,
    double initial_stretch) {
    const double amplitude = std::sqrt(coherent_state_basis_weight(n, initial_stretch));
    const double relative_phase = 2.0 * std::numbers::pi * n * phase;
    return amplitude * harmonic_basis_wavefunction(n, x) * std::cos(relative_phase);
}

double harmonic_displaced_ground_state(double x, double displacement) {
    const double offset = x - displacement;
    return std::exp(-0.5 * offset * offset) / std::pow(std::numbers::pi, 0.25);
}

double harmonic_displaced_state_coefficient(int n, double displacement) {
    if (n < 0 || !std::isfinite(displacement)) {
        return 0.0;
    }
    double coefficient = std::exp(-0.25 * displacement * displacement);
    for (int order = 1; order <= n; ++order) {
        coefficient *= displacement / std::sqrt(2.0 * order);
    }
    return coefficient;
}

double harmonic_displaced_captured_fraction(int basis_count, double displacement) {
    if (basis_count <= 0 || !std::isfinite(displacement)) {
        return 0.0;
    }
    double captured = 0.0;
    double coefficient = std::exp(-0.25 * displacement * displacement);
    for (int n = 0; n < basis_count; ++n) {
        captured += coefficient * coefficient;
        coefficient *= displacement / std::sqrt(2.0 * (n + 1));
    }
    return std::clamp(captured, 0.0, 1.0);
}

QVector<QPointF> sample_harmonic_displaced_approximation(
    int basis_count,
    double x_min,
    double x_max,
    int sample_count,
    double displacement) {
    QVector<QPointF> samples;
    if (basis_count <= 0 || basis_count > 200 || !(x_max > x_min)
        || sample_count < 2 || !std::isfinite(displacement)) {
        return samples;
    }

    QVector<double> coefficients;
    coefficients.reserve(basis_count);
    double coefficient = std::exp(-0.25 * displacement * displacement);
    for (int n = 0; n < basis_count; ++n) {
        coefficients.push_back(coefficient);
        coefficient *= displacement / std::sqrt(2.0 * (n + 1));
    }

    samples.reserve(sample_count);
    for (int i = 0; i < sample_count; ++i) {
        const double x = x_min + (x_max - x_min) * i / double(sample_count - 1);
        double value = 0.0;
        for (int n = 0; n < basis_count; ++n) {
            value += coefficients.at(n) * harmonic_basis_wavefunction(n, x);
        }
        samples.push_back(QPointF(x, value));
    }
    return samples;
}
