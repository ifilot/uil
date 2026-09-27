#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

/** @brief Dimensionless observables for a displaced harmonic-oscillator packet. */
struct HarmonicWavepacketObservation {
    double phase = 0.0;
    double mean_position = 0.0;
    double mean_momentum = 0.0;
    double potential_at_mean = 0.0;
    QString stage;
};

/** @brief Result of advancing finite one-period playback from a starting phase. */
struct HarmonicPlaybackSample {
    double phase = 0.0;
    bool running = false;
};

/** @brief Evaluates the coherent-state center and presentation-stage description. */
HarmonicWavepacketObservation evaluate_harmonic_wavepacket(
    double phase,
    double initial_stretch);

/** @brief Evaluates the normalized dimensionless Gaussian density at @p x. */
double harmonic_probability_density(double x, double mean_position);

/** @brief Samples the Gaussian density uniformly, including both range endpoints. */
QVector<QPointF> sample_harmonic_probability_density(
    double x_min,
    double x_max,
    int sample_count,
    double mean_position);

/** @brief Computes monotonic elapsed-time playback, clamped at one period. */
HarmonicPlaybackSample harmonic_playback_sample(
    double starting_phase,
    double elapsed_seconds,
    double period_seconds,
    bool loop = false);

/** @brief Evaluates the normalized probability density of eigenstate @p n. */
double harmonic_basis_probability_density(int n, double x);

/** @brief Evaluates the real, normalized spatial eigenfunction of state @p n. */
double harmonic_basis_wavefunction(int n, double x);

/** @brief Returns the coherent-state probability weight of eigenstate @p n. */
double coherent_state_basis_weight(int n, double initial_stretch);

/** @brief Returns the real part of a weighted coherent-state basis component.
 *
 * The common zero-point phase is omitted, leaving the physically relevant
 * relative phase exp(-i 2 pi n tau).
 */
double coherent_state_basis_real_component(
    int n,
    double x,
    double phase,
    double initial_stretch);

/** @brief Evaluates the displaced ground state Phi(x) = psi_0(x-d). */
double harmonic_displaced_ground_state(double x, double displacement);

/** @brief Returns <psi_n|Phi> for a ground state displaced by @p displacement. */
double harmonic_displaced_state_coefficient(int n, double displacement);

/** @brief Returns the norm fraction captured by states n = 0 through N - 1. */
double harmonic_displaced_captured_fraction(int basis_count, double displacement);

/** @brief Samples the N-state approximation of a displaced ground state. */
QVector<QPointF> sample_harmonic_displaced_approximation(
    int basis_count,
    double x_min,
    double x_max,
    int sample_count,
    double displacement);
