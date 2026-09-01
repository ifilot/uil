#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>

struct MoleculeAtom {
    QString element;
    QVector3D position;
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
};

/**
 * @brief Loads a bounded XYZ molecular geometry and infers covalent bonds.
 * @param path XYZ file path with Cartesian coordinates in angstrom.
 * @param geometry Receives the parsed geometry on success.
 * @param error_message Receives a user-facing failure description when provided.
 * @return True when a valid geometry was loaded.
 */
bool load_xyz_molecule(
    const QString& path,
    MoleculeGeometry* geometry,
    QString* error_message = nullptr);
