#include "molecule/molecule_geometry.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

namespace {
bool write_xyz(const QString& path, const QByteArray& contents) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents) == contents.size();
}
}  // namespace

class MoleculeGeometryTest final : public QObject {
  Q_OBJECT

 private slots:
  /** @brief Verifies that ordinary XYZ rows remain static. */
  void loads_plain_xyz_without_vibration();
  /** @brief Verifies normal-mode parsing and sinusoidal displacement. */
  void loads_and_applies_normal_mode_vectors();
  /** @brief Verifies malformed displacement vectors are rejected explicitly. */
  void rejects_invalid_normal_mode_vectors();
};

void MoleculeGeometryTest::loads_plain_xyz_without_vibration() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("water.xyz"));
  QVERIFY(write_xyz(path, QByteArrayLiteral("3\nWater\n"
                                            "O 0.0000 0.0000 0.0000\n"
                                            "H 0.9572 0.0000 0.0000\n"
                                            "H -0.2390 0.9270 0.0000\n")));

  MoleculeGeometry geometry;
  QString error_message;
  QVERIFY2(load_xyz_molecule(path, &geometry, &error_message), qPrintable(error_message));
  QVERIFY(geometry.is_valid());
  QVERIFY(!geometry.has_vibration());
  QCOMPARE(geometry.positions_at_phase(1.0f).at(1), geometry.atoms.at(1).position);
}

void MoleculeGeometryTest::loads_and_applies_normal_mode_vectors() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("water-bend.xyz"));
  QVERIFY(write_xyz(path, QByteArrayLiteral("3\nWater bend\n"
                                            "O  0.0000  0.0000 0.0000  0.0000 -0.1000 0.0000\n"
                                            "H  0.9572  0.0000 0.0000  0.0000  0.2400 0.0000\n"
                                            "H -0.2390  0.9270 0.0000  0.2200 -0.0600 0.0000\n")));

  MoleculeGeometry geometry;
  QString error_message;
  QVERIFY2(load_xyz_molecule(path, &geometry, &error_message), qPrintable(error_message));
  QVERIFY(geometry.has_vibration());
  QCOMPARE(geometry.atoms.at(0).vibration, QVector3D(0.0f, -0.1f, 0.0f));

  const QVector<QVector3D> equilibrium = geometry.positions_at_phase(0.0f);
  const QVector<QVector3D> positive_extreme =
      geometry.positions_at_phase(float(std::acos(-1.0) * 0.5));
  QCOMPARE(equilibrium.at(1), geometry.atoms.at(1).position);
  QVERIFY(qAbs(positive_extreme.at(1).y() - (geometry.atoms.at(1).position.y() + 0.24f)) < 1.0e-5f);
}

void MoleculeGeometryTest::rejects_invalid_normal_mode_vectors() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("invalid-mode.xyz"));
  QVERIFY(write_xyz(path, QByteArrayLiteral("1\nInvalid mode\nH 0 0 0 not-a-number 0 0\n")));

  MoleculeGeometry geometry;
  QString error_message;
  QVERIFY(!load_xyz_molecule(path, &geometry, &error_message));
  QVERIFY(error_message.contains(QStringLiteral("vibration vector")));
  QVERIFY(!geometry.is_valid());
}

QTEST_GUILESS_MAIN(MoleculeGeometryTest)

#include "molecule_geometry_test.moc"
