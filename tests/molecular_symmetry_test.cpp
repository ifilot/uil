#include "symmetry/molecular_symmetry.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
  /** @brief Validates author-defined orbital selections and rejects unsafe/ambiguous entries. */
  void parses_orbital_selections();
  /** @brief Verifies baked signed meshes, including the radial node of 2s. */
  void baked_orbitals_preserve_phase();
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
  QCOMPARE(definition.operations.at(1).type, MolecularSymmetryOperationType::ProperRotation);
  QCOMPARE(definition.operation_colors.identity, QColor(QStringLiteral("#112233")));
  QCOMPARE(definition.operation_colors.rotation, QColor(QStringLiteral("#9b2f4f")));
  QCOMPARE(definition.operation_colors.inversion, QColor(122, 90, 166));
}

void MolecularSymmetryTest::computes_fractional_transforms() {
  MolecularSymmetryOperation rotation{QStringLiteral("C2(z)"),
                                      MolecularSymmetryOperationType::ProperRotation, 2, 1,
                                      QVector3D(0, 0, 1)};
  const QVector3D quarter = rotation.matrix_at(0.5).mapVector(QVector3D(1, 0, 0));
  QVERIFY(qAbs(quarter.x()) < 1.0e-5f);
  QVERIFY(qAbs(quarter.y() - 1.0f) < 1.0e-5f);

  MolecularSymmetryOperation negative_rotation{QStringLiteral("C3(-120)"),
                                               MolecularSymmetryOperationType::ProperRotation, 3, 2,
                                               QVector3D(0, 0, 1)};
  const QVector3D signed_halfway = negative_rotation.matrix_at(0.5).mapVector(QVector3D(1, 0, 0));
  QVERIFY(qAbs(signed_halfway.x() - 0.5f) < 1.0e-5f);
  QVERIFY(qAbs(signed_halfway.y() + 0.8660254f) < 1.0e-5f);

  MolecularSymmetryOperation reflection{QStringLiteral("sigma(yz)"),
                                        MolecularSymmetryOperationType::Reflection, 1, 1,
                                        QVector3D(1, 0, 0)};
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
    QFile file(QStringLiteral(UIL_TEST_SOURCE_DIR "/examples/molecular-symmetry/") +
               files.at(index));
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

void MolecularSymmetryTest::parses_orbital_selections() {
  auto root = QJsonDocument::fromJson(water_payload()).object();
  root["orbitals"] = QJsonArray{QJsonObject{{"atom", 1}, {"orbital", "2px"}, {"scale", 1.2}}};
  MolecularSymmetryDefinition definition;
  QString error;
  QVERIFY(parse_molecular_symmetry(QJsonDocument(root).toJson(), &definition, &error));
  QCOMPARE(definition.orbitals.size(), 1);
  QCOMPARE(definition.orbitals.at(0).atom, 1);
  QCOMPARE(definition.orbitals.at(0).scale, 1.2f);
  const auto original = root["orbitals"].toArray().at(0).toObject();
  for (const auto& change : {QJsonObject{{"atom", 0}}, QJsonObject{{"atom", 4}},
                             QJsonObject{{"atom", 1.5}}, QJsonObject{{"orbital", "4fxyz"}},
                             QJsonObject{{"scale", 0}}, QJsonObject{{"scale", "large"}}}) {
    auto entry = original;
    for (auto it = change.begin(); it != change.end(); ++it) entry[it.key()] = it.value();
    root["orbitals"] = QJsonArray{entry};
    QVERIFY(!parse_molecular_symmetry(QJsonDocument(root).toJson(), &definition, &error));
  }
  root["orbitals"] = QJsonArray{original, original};
  QVERIFY(!parse_molecular_symmetry(QJsonDocument(root).toJson(), &definition, &error));
  root["orbitals"] = QJsonArray{QJsonObject{{"atom", 1}, {"orbital", "2px"}},
                                QJsonObject{{"atom", 2}, {"orbital", "1s"}},
                                QJsonObject{{"atom", 3}, {"orbital", "1s"}, {"scale", 0.7}}};
  QVERIFY(parse_molecular_symmetry(QJsonDocument(root).toJson(), &definition, &error));
  QCOMPARE(definition.orbitals.at(0).scale, 0.85f);
  QCOMPARE(definition.orbitals.at(1).scale, 0.45f);
  QCOMPARE(definition.orbitals.at(2).scale, 0.7f);
}

void MolecularSymmetryTest::baked_orbitals_preserve_phase() {
  for (const auto& name : symmetry_orbital_names()) {
    const auto& mesh = symmetry_orbital_mesh(name);
    QVERIFY(!mesh.positive.isEmpty());
    if (name != "1s") QVERIFY(!mesh.negative.isEmpty());
    for (const auto* phase : {&mesh.positive, &mesh.negative}) {
      QCOMPARE(phase->size() % 3, 0);
      for (const auto& vertex : *phase) {
        QVERIFY(vertex.position.length() <= 1.0001f);
        QVERIFY(std::abs(vertex.normal.length() - 1.0f) < 0.001f);
        if (name == "2px") {
          QVERIFY(phase == &mesh.positive ? vertex.position.x() > 0 : vertex.position.x() < 0);
        }
        if (name == "3dxy") {
          const float sign = vertex.position.x() * vertex.position.y();
          QVERIFY(phase == &mesh.positive ? sign > 0 : sign < 0);
        }
      }
    }
  }
  const auto& radial = symmetry_orbital_mesh("2s");
  float positive_radius = 0, negative_radius = 2;
  for (const auto& vertex : radial.positive)
    positive_radius = std::max(positive_radius, vertex.position.length());
  for (const auto& vertex : radial.negative)
    negative_radius = std::min(negative_radius, vertex.position.length());
  QVERIFY(positive_radius < negative_radius);
}

#include "molecular_symmetry_test.moc"
