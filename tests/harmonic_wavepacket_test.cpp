#include "figure/harmonic_wavepacket.hpp"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>

class HarmonicWavepacketTest final : public QObject {
    Q_OBJECT

private slots:
    void characteristic_phases_match_coherent_state();
    void gaussian_is_normalized_and_peaks_at_mean();
    void playback_clamps_and_stops_after_one_period();
    void first_six_basis_functions_are_normalized();
    void coherent_state_weights_follow_poisson_distribution();
    void real_components_follow_relative_phase();
};

void HarmonicWavepacketTest::characteristic_phases_match_coherent_state() {
    constexpr double amplitude = 3.0;
    const auto initial = evaluate_harmonic_wavepacket(0.0, amplitude);
    QCOMPARE(initial.mean_position, amplitude);
    QVERIFY(std::abs(initial.mean_momentum) < 1e-12);

    const auto quarter = evaluate_harmonic_wavepacket(0.25, amplitude);
    QVERIFY(std::abs(quarter.mean_position) < 1e-12);
    QVERIFY(std::abs(quarter.mean_momentum + amplitude) < 1e-12);

    const auto half = evaluate_harmonic_wavepacket(0.5, amplitude);
    QVERIFY(std::abs(half.mean_position + amplitude) < 1e-12);
    QVERIFY(std::abs(half.mean_momentum) < 1e-12);

    const auto complete = evaluate_harmonic_wavepacket(1.0, amplitude);
    QVERIFY(std::abs(complete.mean_position - amplitude) < 1e-12);
    QVERIFY(std::abs(complete.mean_momentum) < 1e-12);
}

void HarmonicWavepacketTest::gaussian_is_normalized_and_peaks_at_mean() {
    constexpr double mean = 1.37;
    const QVector<QPointF> samples = sample_harmonic_probability_density(-8.0, 8.0, 16001, mean);
    QVERIFY(!samples.isEmpty());

    double integral = 0.0;
    for (int i = 1; i < samples.size(); ++i) {
        const double dx = samples.at(i).x() - samples.at(i - 1).x();
        integral += 0.5 * dx * (samples.at(i).y() + samples.at(i - 1).y());
    }
    QVERIFY(std::abs(integral - 1.0) < 1e-6);

    const auto peak = std::max_element(samples.cbegin(), samples.cend(), [](const QPointF& lhs, const QPointF& rhs) {
        return lhs.y() < rhs.y();
    });
    QVERIFY(peak != samples.cend());
    QVERIFY(std::abs(peak->x() - mean) <= 0.001);
    QVERIFY(std::abs(peak->y() - 1.0 / std::sqrt(std::numbers::pi)) < 1e-9);
}

void HarmonicWavepacketTest::playback_clamps_and_stops_after_one_period() {
    const HarmonicPlaybackSample midway = harmonic_playback_sample(0.25, 2.0, 8.0);
    QCOMPARE(midway.phase, 0.5);
    QVERIFY(midway.running);

    const HarmonicPlaybackSample complete = harmonic_playback_sample(0.25, 8.0, 8.0);
    QCOMPARE(complete.phase, 1.0);
    QVERIFY(!complete.running);

    const HarmonicPlaybackSample invalid = harmonic_playback_sample(0.4, 2.0, 0.0);
    QCOMPARE(invalid.phase, 0.4);
    QVERIFY(!invalid.running);

    const HarmonicPlaybackSample looped = harmonic_playback_sample(0.75, 4.0, 8.0, true);
    QCOMPARE(looped.phase, 0.25);
    QVERIFY(looped.running);
}

void HarmonicWavepacketTest::first_six_basis_functions_are_normalized() {
    constexpr double x_min = -9.0;
    constexpr double x_max = 9.0;
    constexpr int samples = 36001;
    const double dx = (x_max - x_min) / (samples - 1);
    for (int n = 0; n < 6; ++n) {
        double integral = 0.0;
        double previous = harmonic_basis_probability_density(n, x_min);
        for (int i = 1; i < samples; ++i) {
            const double current = harmonic_basis_probability_density(n, x_min + i * dx);
            integral += 0.5 * dx * (previous + current);
            previous = current;
        }
        QVERIFY(std::abs(integral - 1.0) < 1e-8);
    }
}

void HarmonicWavepacketTest::real_components_follow_relative_phase() {
    constexpr double x = 0.37;
    constexpr double stretch = 3.0;
    const double ground_initial = coherent_state_basis_real_component(0, x, 0.0, stretch);
    const double ground_quarter = coherent_state_basis_real_component(0, x, 0.25, stretch);
    QVERIFY(std::abs(ground_initial - ground_quarter) < 1e-12);

    const double first_initial = coherent_state_basis_real_component(1, x, 0.0, stretch);
    const double first_quarter = coherent_state_basis_real_component(1, x, 0.25, stretch);
    const double first_half = coherent_state_basis_real_component(1, x, 0.5, stretch);
    QVERIFY(std::abs(first_quarter) < 1e-12);
    QVERIFY(std::abs(first_half + first_initial) < 1e-12);
}

void HarmonicWavepacketTest::coherent_state_weights_follow_poisson_distribution() {
    constexpr double stretch = 3.0;
    const double mean_occupation = 0.5 * stretch * stretch;
    const double ground_weight = coherent_state_basis_weight(0, stretch);
    QVERIFY(std::abs(ground_weight - std::exp(-mean_occupation)) < 1e-12);
    for (int n = 0; n < 3; ++n) {
        const double ratio = coherent_state_basis_weight(n + 1, stretch)
            / coherent_state_basis_weight(n, stretch);
        QVERIFY(std::abs(ratio - mean_occupation / (n + 1)) < 1e-12);
    }
}

QTEST_GUILESS_MAIN(HarmonicWavepacketTest)

#include "harmonic_wavepacket_test.moc"
