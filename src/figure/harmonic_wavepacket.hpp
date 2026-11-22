#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

/*
 * All quantities are dimensionless: positions are in units of the oscillator length
 * sqrt(hbar / (m omega)), energies in units of hbar omega, and phases in oscillation periods.
 */

/** @brief Dimensionless observables for a displaced harmonic-oscillator packet. */
struct HarmonicWavepacketObservation {
    /** @brief Phase in periods, clamped to [0, 1]. */
    double phase = 0.0;
    /** @brief Packet center <x>. */
    double mean_position = 0.0;
    /** @brief Mean momentum <p> in units of hbar divided by the oscillator length. */
    double mean_momentum = 0.0;
    /** @brief Potential energy x^2 / 2 at the packet center. */
    double potential_at_mean = 0.0;
    /** @brief Short caption describing the bond motion, such as "maximum compression". */
    QString stage;
};

/** @brief Result of advancing finite one-period playback from a starting phase. */
struct HarmonicPlaybackSample {
    /** @brief Phase in periods, within [0, 1]. */
    double phase = 0.0;
    /** @brief Whether playback should continue after this sample. */
    bool running = false;
};

/**
 * @brief Evaluates the coherent-state center and presentation-stage description.
 * @param phase Phase in periods; clamped to [0, 1].
 * @param initial_stretch Packet center at phase 0 (maximum stretch).
 */
HarmonicWavepacketObservation evaluate_harmonic_wavepacket(
    double phase,
    double initial_stretch);

/**
 * @brief Evaluates the normalized coherent-state density exp(-(x - x0)^2) / sqrt(pi) at @p x.
 * @param mean_position Packet center x0.
 */
double harmonic_probability_density(double x, double mean_position);

/**
 * @brief Samples the Gaussian density uniformly, including both range endpoints.
 * @return @p sample_count points, or none when @p x_max <= @p x_min or @p sample_count < 2.
 */
QVector<QPointF> sample_harmonic_probability_density(
    double x_min,
    double x_max,
    int sample_count,
    double mean_position);

/**
 * @brief Computes elapsed-time playback from @p starting_phase.
 *
 * Without @p loop, the phase advances monotonically and stops at 1 (one period); with
 * @p loop, it wraps into [0, 1) and keeps running. A non-positive period or negative elapsed
 * time returns the clamped start phase, not running.
 */
HarmonicPlaybackSample harmonic_playback_sample(
    double starting_phase,
    double elapsed_seconds,
    double period_seconds,
    bool loop = false);

/** @brief Evaluates |psi_n(x)|^2 for eigenstate @p n; 0 outside n in [0, 32]. */
double harmonic_basis_probability_density(int n, double x);

/**
 * @brief Evaluates the real, normalized spatial eigenfunction psi_n(x) of state @p n.
 *
 * Uses the Hermite recurrence; returns 0 for n outside [0, 32], where the plain
 * double-precision evaluation is no longer reliable.
 */
double harmonic_basis_wavefunction(int n, double x);

/**
 * @brief Returns the coherent-state probability weight of eigenstate @p n.
 *
 * The Poisson weight exp(-nbar) nbar^n / n! with mean occupation nbar = initial_stretch^2 / 2;
 * 0 for negative @p n.
 */
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

/**
 * @brief Returns <psi_n|Phi> for a ground state displaced by @p displacement.
 * @return The real expansion coefficient, or 0 for negative @p n or a non-finite displacement.
 */
double harmonic_displaced_state_coefficient(int n, double displacement);

/**
 * @brief Returns the norm fraction captured by states n = 0 through @p basis_count - 1.
 * @return A value in [0, 1]; 0 for a non-positive count or non-finite displacement.
 */
double harmonic_displaced_captured_fraction(int basis_count, double displacement);

/**
 * @brief Samples the @p basis_count-state approximation of a displaced ground state.
 * @return @p sample_count uniformly spaced points including both endpoints, or none when
 *         @p basis_count is outside [1, 200], the range is empty, @p sample_count < 2, or the
 *         displacement is not finite.
 */
QVector<QPointF> sample_harmonic_displaced_approximation(
    int basis_count,
    double x_min,
    double x_max,
    int sample_count,
    double displacement);
