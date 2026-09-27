#include "orbital/symmetry_orbitals.hpp"

float default_symmetry_orbital_radius(const QString& element) {
  // Compact hydrogen/helium shells keep neighbouring atom orbitals readable.
  // Preserve the established scale for other elements rather than implying
  // that these normalized teaching surfaces predict physical orbital radii.
  if (element == QStringLiteral("H") || element == QStringLiteral("He")) return 0.45f;
  return 0.85f;
}

#include <QDataStream>
#include <QSet>
#include <array>
#include <cmath>
#include <mutex>

namespace {
#include "symmetry_orbitals_data.inc"
}

const SymmetryOrbitalMesh& symmetry_orbital_mesh(const QString& name) {
  static std::array<SymmetryOrbitalMesh, 10> meshes;
  static std::array<std::once_flag, 10> initialized;
  static const SymmetryOrbitalMesh empty;
  const int index = symmetry_orbital_names().indexOf(name);
  if (index < 0) return empty;
  std::call_once(initialized[index], [index] {
    const QByteArray bytes = qUncompress(QByteArray::fromBase64(kBakedOrbitals[index]));
    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (auto* phase : {&meshes[index].positive, &meshes[index].negative}) {
      quint32 count;
      stream >> count;
      QVector<SymmetryOrbitalVertex> unique;
      unique.reserve(count);
      for (quint32 vertex = 0; vertex < count; ++vertex) {
        qint16 x, y, z, nx, ny, nz;
        stream >> x >> y >> z >> nx >> ny >> nz;
        unique.push_back(
            {QVector3D(x, y, z) / 32767.0f, (QVector3D(nx, ny, nz) / 32767.0f).normalized()});
      }
      stream >> count;
      phase->reserve(count);
      for (quint32 vertex = 0; vertex < count; ++vertex) {
        quint32 id;
        stream >> id;
        phase->push_back(unique.at(id));
      }
    }
  });
  return meshes[index];
}

bool valid_symmetry_orbitals(const QVector<SymmetryOrbital>& orbitals, int atom_count) {
  if (orbitals.size() > 64) return false;
  QSet<QString> seen;
  for (const auto& orbital : orbitals) {
    const QString key = QString::number(orbital.atom) + ':' + orbital.orbital;
    if (orbital.atom < 1 || orbital.atom > atom_count ||
        !symmetry_orbital_names().contains(orbital.orbital) || !std::isfinite(orbital.scale) ||
        orbital.scale < 0.1f || orbital.scale > 3.0f || seen.contains(key))
      return false;
    seen.insert(key);
  }
  return true;
}
