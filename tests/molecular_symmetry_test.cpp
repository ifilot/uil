#include "symmetry/molecular_symmetry.hpp"

#include <QFile>
#include <QTest>

namespace {
QByteArray water_payload() {
  return QByteArrayLiteral(R"json({
    "format": "uil.molecular-symmetry",
    "version": 1,
    "title": "Water symmetry",
    "point_group": "C2v",
    "animation_duration_ms": 800,
    "operation_colors": {
      "identity":"#112233", "rotation":"#9b2f4f", "reflection":"#277d83"
    },
    "molecule": {
      "xyz": "3\nWater\nO 0 0 0\nH 0 0.757 0.58605\nH 0 -0.757 0.58605\n"
    },
    "operations": [
      {"label":"E", "type":"identity"},
      {"label":"C2(z)", "type":"rotation", "order":2, "axis":[0,0,1]},
      {"label":"sigma-v(xz)", "type":"reflection", "axis":[0,1,0]},
      {"label":"sigma-v(yz)", "type":"reflection", "axis":[1,0,0]}
    ]
  })json");
}
}  // namespace

class MolecularSymmetryTest final : public QObject {
  Q_OBJECT

 private slots:
  void parses_complete_operation_set();
  void computes_fractional_transforms();
  void rejects_operation_that_does_not_preserve_molecule();
  void rejects_invalid_operation_color();
  void bundled_example_payloads_are_valid();
};

void MolecularSymmetryTest::parses_complete_operation_set() {
  MolecularSymmetryDefinition definition;
  QString error;
  QVERIFY2(parse_molecular_symmetry(water_payload(), &definition, &error), qPrintable(error));
  QCOMPARE(definition.title, QStringLiteral("Water symmetry"));
  QCOMPARE(definition.point_group, QStringLiteral("C2v"));
  QCOMPARE(definition.geometry.atoms.size(), 3);
  QCOMPARE(definition.geometry.atoms.constFirst().element, QStringLiteral("O"));
  QCOMPARE(definition.geometry.atoms.constFirst().position, QVector3D());
  QCOMPARE(definition.operations.size(), 4);
  QCOMPARE(definition.operations.at(1).type,
           MolecularSymmetryOperationType::ProperRotation);
  QCOMPARE(definition.operation_colors.identity, QColor(QStringLiteral("#112233")));
  QCOMPARE(definition.operation_colors.rotation, QColor(QStringLiteral("#9b2f4f")));
  QCOMPARE(definition.operation_colors.inversion, QColor(122, 90, 166));
}

void MolecularSymmetryTest::computes_fractional_transforms() {
  MolecularSymmetryOperation rotation{
      QStringLiteral("C2(z)"), MolecularSymmetryOperationType::ProperRotation,
      2, 1, QVector3D(0, 0, 1)};
  const QVector3D quarter = rotation.matrix_at(0.5).mapVector(QVector3D(1, 0, 0));
  QVERIFY(qAbs(quarter.x()) < 1.0e-5f);
  QVERIFY(qAbs(quarter.y() - 1.0f) < 1.0e-5f);

  MolecularSymmetryOperation negative_rotation{
      QStringLiteral("C3(-120)"), MolecularSymmetryOperationType::ProperRotation,
      3, 2, QVector3D(0, 0, 1)};
  const QVector3D signed_halfway =
      negative_rotation.matrix_at(0.5).mapVector(QVector3D(1, 0, 0));
  QVERIFY(qAbs(signed_halfway.x() - 0.5f) < 1.0e-5f);
  QVERIFY(qAbs(signed_halfway.y() + 0.8660254f) < 1.0e-5f);

  MolecularSymmetryOperation reflection{
      QStringLiteral("sigma(yz)"), MolecularSymmetryOperationType::Reflection,
      1, 1, QVector3D(1, 0, 0)};
  const QVector3D reflected = reflection.matrix_at(1.0).mapVector(QVector3D(2, 3, 4));
  QCOMPARE(reflected, QVector3D(-2, 3, 4));
}

void MolecularSymmetryTest::rejects_invalid_operation_color() {
  QByteArray invalid = water_payload();
  invalid.replace("#112233", "transparent");
  MolecularSymmetryDefinition definition;
  QString error;
  QVERIFY(!parse_molecular_symmetry(invalid, &definition, &error));
  QVERIFY(error.contains(QStringLiteral("#RRGGBB")));
}

void MolecularSymmetryTest::rejects_operation_that_does_not_preserve_molecule() {
  QByteArray invalid = water_payload();
  invalid.replace("[0,0,1]", "[1,0,0]");
  MolecularSymmetryDefinition definition;
  QString error;
  QVERIFY(!parse_molecular_symmetry(invalid, &definition, &error));
  QVERIFY(error.contains(QStringLiteral("does not map")));
}

void MolecularSymmetryTest::bundled_example_payloads_are_valid() {
  const QStringList files{
      QStringLiteral("water.uilsym"),
      QStringLiteral("ammonia.uilsym"),
      QStringLiteral("boron-trifluoride.uilsym"),
      QStringLiteral("ethylene.uilsym"),
      QStringLiteral("methane.uilsym"),
  };
  const QList<int> expected_operation_counts{4, 6, 12, 8, 24};
  for (int index = 0; index < files.size(); ++index) {
    QFile file(QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/molecular-symmetry/")
               + files.at(index));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    MolecularSymmetryDefinition definition;
    QString error;
    QVERIFY2(parse_molecular_symmetry(file.readAll(), &definition, &error),
             qPrintable(files.at(index) + QStringLiteral(": ") + error));
    QCOMPARE(definition.operations.size(), expected_operation_counts.at(index));
    if (index == 0 || index == 1) {
      QCOMPARE(definition.geometry.atoms.constFirst().position, QVector3D());
      QCOMPARE(definition.geometry.atoms.constFirst().element,
               index == 0 ? QStringLiteral("O") : QStringLiteral("N"));
    }
  }
}

QTEST_GUILESS_MAIN(MolecularSymmetryTest)

#include "molecular_symmetry_test.moc"
