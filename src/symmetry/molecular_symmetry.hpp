#pragma once

#include "molecule/molecule_geometry.hpp"

#include <QByteArray>
#include <QColor>
#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector3D>

/** @brief Kind of finite molecular point-group operation. */
enum class MolecularSymmetryOperationType {
  Identity,
  ProperRotation,
  Reflection,
  Inversion,
  ImproperRotation,
};

/** @brief User-configurable colors for the symmetry-operation classes. */
struct MolecularSymmetryOperationColors {
  QColor identity{95, 107, 120};
  QColor rotation{155, 47, 79};
  QColor reflection{39, 125, 131};
  QColor inversion{122, 90, 166};
  QColor improper_rotation{192, 106, 43};

  /** @brief Returns the color assigned to @p type. */
  QColor for_type(MolecularSymmetryOperationType type) const;
  /** @brief Returns whether every configured color can be rendered. */
  bool is_valid() const;
};

/** @brief One clickable point-group operation acting around the molecule center. */
struct MolecularSymmetryOperation {
  QString label;
  MolecularSymmetryOperationType type = MolecularSymmetryOperationType::Identity;
  int order = 1;
  int power = 1;
  QVector3D axis;

  /** @brief Returns whether the operation parameters are internally consistent. */
  bool is_valid() const;
  /** @brief Returns the fractional operation transform for animation progress [0, 1]. */
  QMatrix4x4 matrix_at(double progress) const;
};

/** @brief Complete embedded definition for a symmetry-operation molecule widget. */
struct MolecularSymmetryDefinition {
  QString title;
  QString point_group;
  MoleculeGeometry geometry;
  QVector<MolecularSymmetryOperation> operations;
  MolecularSymmetryOperationColors operation_colors;
  int animation_duration_ms = 1100;

  /** @brief Returns whether the geometry and operation collection are usable. */
  bool is_valid() const;
};

/** @brief Parses and validates a self-contained `uil.molecular-symmetry` payload. */
bool parse_molecular_symmetry(const QByteArray& payload,
                              MolecularSymmetryDefinition* definition,
                              QString* error_message = nullptr);

/** @brief Returns a concise user-facing name for an operation kind. */
QString molecular_symmetry_operation_type_name(MolecularSymmetryOperationType type);
