#pragma once

#include <QStringList>
#include <QVector3D>
#include <QVector>

/** @brief A selected real orbital attached to a one-based atom index in the XYZ file. */
struct SymmetryOrbital {
  int atom = 1;
  QString orbital = QStringLiteral("2pz");
  float scale = 0.85f;
  bool operator==(const SymmetryOrbital&) const = default;
};

/** @brief Returns the supported basis in stable menu and baked-data order. */
inline QStringList symmetry_orbital_names() {
  return {"1s", "2s", "2px", "2py", "2pz", "3dxy", "3dxz", "3dyz", "3dx2-y2", "3dz2"};
}

/** @brief Supported occupied subshells of neutral ground-state atoms, not a molecular occupancy
 * model. */
inline QStringList occupied_symmetry_orbital_names(const QString& element) {
  const QStringList elements =
      QStringLiteral(
          "H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge "
          "As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd Pm Sm "
          "Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th Pa U "
          "Np Pu Am Cm Bk Cf Es Fm Md No Lr Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og")
          .split(' ');
  const int z = elements.indexOf(element, 0) + 1;
  QStringList result;
  if (z >= 1) result << "1s";
  if (z >= 3) result << "2s";
  if (z >= 5) result << "2px" << "2py" << "2pz";
  if (z >= 21) result << "3dxy" << "3dxz" << "3dyz" << "3dx2-y2" << "3dz2";
  return result;
}

/** @brief Position and outward normal of a baked, unit-radius isosurface vertex. */
struct SymmetryOrbitalVertex {
  QVector3D position;
  QVector3D normal;
};

/** @brief Independent positive and negative phase triangle lists, generated offline. */
struct SymmetryOrbitalMesh {
  QVector<SymmetryOrbitalVertex> positive;
  QVector<SymmetryOrbitalVertex> negative;
};

/** @brief Decodes an embedded mesh once; never samples or meshes a wavefunction at runtime. */
const SymmetryOrbitalMesh& symmetry_orbital_mesh(const QString& name);

/** @brief Validates unique atom/basis selections and bounded display radii. */
bool valid_symmetry_orbitals(const QVector<SymmetryOrbital>& orbitals, int atom_count);

/** @brief Illustrative element-aware display radius, not a physical orbital extent. */
float default_symmetry_orbital_radius(const QString& element);
