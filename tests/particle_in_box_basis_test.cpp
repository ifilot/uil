#include "figure/interactive_figure.hpp"
#include "figure/particle_in_box_basis.hpp"

#include <QTest>

#include <cmath>
#include <numbers>

class ParticleInBoxBasisTest final : public QObject {
    Q_OBJECT

private slots:
    void coefficients_match_slide_derivation();
    void captured_percentages_match_slide_values();
    void captured_fraction_increases_through_twenty_five_terms();
    void partial_sum_obeys_box_boundaries();
    void interactive_figure_payload_parses();
};

void ParticleInBoxBasisTest::coefficients_match_slide_derivation() {
    const double expected = 2.0 / std::numbers::pi;
    QVERIFY(std::abs(particle_in_box_step_coefficient(1) - expected) < 1e-12);
    QVERIFY(std::abs(particle_in_box_step_coefficient(2) + expected) < 1e-12);
    QVERIFY(std::abs(particle_in_box_step_coefficient(3) - expected / 3.0) < 1e-12);
    QVERIFY(std::abs(particle_in_box_step_coefficient(4)) < 1e-12);
    QVERIFY(std::abs(particle_in_box_step_coefficient(5) - expected / 5.0) < 1e-12);
}

void ParticleInBoxBasisTest::captured_percentages_match_slide_values() {
    QVERIFY(std::abs(100.0 * particle_in_box_step_captured_fraction(1) - 40.5) < 0.1);
    QVERIFY(std::abs(100.0 * particle_in_box_step_captured_fraction(5) - 87.2) < 0.1);
    QVERIFY(std::abs(100.0 * particle_in_box_step_captured_fraction(25) - 97.5) < 0.1);
}

void ParticleInBoxBasisTest::captured_fraction_increases_through_twenty_five_terms() {
    double previous = 0.0;
    for (int basis_count = 1; basis_count <= 25; ++basis_count) {
        const double current = particle_in_box_step_captured_fraction(basis_count);
        QVERIFY(current + 1e-14 >= previous);
        previous = current;
    }
    QVERIFY(previous < 1.0);
}

void ParticleInBoxBasisTest::partial_sum_obeys_box_boundaries() {
    const QVector<QPointF> samples = sample_particle_in_box_step_approximation(25, 1001);
    QCOMPARE(samples.size(), 1001);
    QVERIFY(std::abs(samples.first().y()) < 1e-12);
    QVERIFY(std::abs(samples.last().y()) < 1e-12);
}

void ParticleInBoxBasisTest::interactive_figure_payload_parses() {
    const QByteArray payload = QByteArrayLiteral(R"json({
      "format": "uil.interactive-figure",
      "version": 1,
      "title": "Step expansion",
      "background_svg": "<svg xmlns='http://www.w3.org/2000/svg'/>",
      "plot": {
        "kind": "particle-in-box-step-expansion",
        "x_min": 0,
        "x_max": 1,
        "y_min": -0.25,
        "y_max": 1.25,
        "x_label": "x",
        "y_label": "f(x)",
        "step_position": 0.5,
        "step_height": 1.0
      },
      "controls": {
        "basis_count": { "min": 1, "max": 25, "value": 1 }
      }
    })json");
    InteractiveFigureDefinition definition;
    QString error_message;
    QVERIFY2(parse_interactive_figure(payload, &definition, &error_message),
             qPrintable(error_message));
    QCOMPARE(
        definition.kind,
        InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion);
    QCOMPARE(definition.basis_count_min, 1);
    QCOMPARE(definition.basis_count_max, 25);
    QCOMPARE(definition.basis_count_initial, 1);
    QCOMPARE(definition.step_position, 0.5);
}

QTEST_GUILESS_MAIN(ParticleInBoxBasisTest)

#include "particle_in_box_basis_test.moc"
