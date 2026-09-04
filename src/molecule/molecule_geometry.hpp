#pragma once

#include <QString>
#include <QVector3D>
#include <QVector>

struct MoleculeAtom {
  QString element;
  QVector3D position;
  QVector3D vibration;
};

struct MoleculeBond {
  int first_atom = -1;
  int second_atom = -1;
};

struct MoleculeGeometry {
  QString description;
  QVector<MoleculeAtom> atoms;
  QVector<MoleculeBond> bonds;

  /** @brief Returns whether the geometry contains at least one atom. */
  bool is_valid() const;
  /** @brief Returns whether at least one atom has a normal-mode displacement. */
  bool has_vibration() const;
  /** @brief Returns atom positions displaced by a sinusoidal vibration phase. */
  QVector<QVector3D> positions_at_phase(float phase_radians, float amplitude = 1.0f) const;
};

/**
 * @brief Loads a bounded XYZ molecular geometry and infers covalent bonds.
 * Atom records may append three normal-mode displacement components after the
 * Cartesian coordinates: `element x y z dx dy dz`.
 * @param path XYZ file path with Cartesian coordinates in angstrom.
 * @param geometry Receives the parsed geometry on success.
 * @param error_message Receives a user-facing failure description when provided.
 * @return True when a valid geometry was loaded.
 */
bool load_xyz_molecule(const QString& path, MoleculeGeometry* geometry,
                       QString* error_message = nullptr);
