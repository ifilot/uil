#include "symmetry/molecular_symmetry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr qsizetype kMaximumPayloadBytes = 4 * 1024 * 1024;
constexpr int kMaximumOperations = 96;
constexpr double kPi = 3.14159265358979323846;

void set_error(QString* error_message, const QString& message) {
  if (error_message) *error_message = message;
}

QMatrix4x4 axis_angle_matrix(QVector3D axis, double radians) {
  axis.normalize();
  const double x = axis.x();
  const double y = axis.y();
  const double z = axis.z();
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  const double t = 1.0 - c;
  QMatrix4x4 matrix;
  matrix.setToIdentity();
  matrix(0, 0) = float(t * x * x + c);
  matrix(0, 1) = float(t * x * y - s * z);
  matrix(0, 2) = float(t * x * z + s * y);
  matrix(1, 0) = float(t * x * y + s * z);
  matrix(1, 1) = float(t * y * y + c);
  matrix(1, 2) = float(t * y * z - s * x);
  matrix(2, 0) = float(t * x * z - s * y);
  matrix(2, 1) = float(t * y * z + s * x);
  matrix(2, 2) = float(t * z * z + c);
  return matrix;
}

int shortest_signed_power(int power, int order) {
  int signed_power = power % order;
  if (signed_power > order / 2) signed_power -= order;
  if (signed_power < -order / 2) signed_power += order;
  return signed_power;
}

bool parse_operation_color(const QJsonObject& colors, const QString& name,
                           QColor* color, QString* error_message) {
  if (!colors.contains(name)) return true;
  const QString value = colors.value(name).toString();
  static const QRegularExpression hex_color(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
  if (!hex_color.match(value).hasMatch()) {
    set_error(error_message,
              QStringLiteral("operation_colors.%1 must be a #RRGGBB color").arg(name));
    return false;
  }
  *color = QColor(value);
  return true;
}

QMatrix4x4 reflection_matrix(QVector3D normal, double amount) {
  normal.normalize();
  QMatrix4x4 matrix;
  matrix.setToIdentity();
  const double components[] = {normal.x(), normal.y(), normal.z()};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      matrix(row, column) = float((row == column ? 1.0 : 0.0)
                                  - 2.0 * amount * components[row] * components[column]);
    }
  }
  return matrix;
}

bool parse_axis(const QJsonValue& value, QVector3D* axis) {
  if (!axis || !value.isArray()) return false;
  const QJsonArray values = value.toArray();
  if (values.size() != 3) return false;
  const double x = values.at(0).toDouble(std::numeric_limits<double>::quiet_NaN());
  const double y = values.at(1).toDouble(std::numeric_limits<double>::quiet_NaN());
  const double z = values.at(2).toDouble(std::numeric_limits<double>::quiet_NaN());
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  *axis = QVector3D(float(x), float(y), float(z));
  return true;
}

bool operation_maps_geometry(const MolecularSymmetryOperation& operation,
                             const MoleculeGeometry& geometry) {
  const QMatrix4x4 matrix = operation.matrix_at(1.0);
  const float tolerance_squared = 0.12f * 0.12f;
  for (const MoleculeAtom& atom : geometry.atoms) {
    const QVector3D transformed = matrix.mapVector(atom.position);
    bool match = false;
    for (const MoleculeAtom& candidate : geometry.atoms) {
      if (candidate.element == atom.element
          && (candidate.position - transformed).lengthSquared() <= tolerance_squared) {
        match = true;
        break;
      }
    }
    if (!match) return false;
  }
  return true;
}
}  // namespace

bool MolecularSymmetryOperation::is_valid() const {
  if (label.trimmed().isEmpty() || label.size() > 80) return false;
  switch (type) {
  case MolecularSymmetryOperationType::Identity:
  case MolecularSymmetryOperationType::Inversion:
    return true;
  case MolecularSymmetryOperationType::Reflection:
    return axis.lengthSquared() > 1.0e-8f;
  case MolecularSymmetryOperationType::ProperRotation:
  case MolecularSymmetryOperationType::ImproperRotation:
    return order >= 2 && order <= 24 && power != 0 && std::abs(power) < order
        && axis.lengthSquared() > 1.0e-8f;
  }
  return false;
}

QColor MolecularSymmetryOperationColors::for_type(
    MolecularSymmetryOperationType type) const {
  switch (type) {
  case MolecularSymmetryOperationType::Identity: return identity;
  case MolecularSymmetryOperationType::ProperRotation: return rotation;
  case MolecularSymmetryOperationType::Reflection: return reflection;
  case MolecularSymmetryOperationType::Inversion: return inversion;
  case MolecularSymmetryOperationType::ImproperRotation: return improper_rotation;
  }
  return identity;
}

bool MolecularSymmetryOperationColors::is_valid() const {
  return identity.isValid() && rotation.isValid() && reflection.isValid()
      && inversion.isValid() && improper_rotation.isValid();
}

QMatrix4x4 MolecularSymmetryOperation::matrix_at(double progress) const {
  progress = std::clamp(progress, 0.0, 1.0);
  QMatrix4x4 identity;
  identity.setToIdentity();
  switch (type) {
  case MolecularSymmetryOperationType::Identity:
    return identity;
  case MolecularSymmetryOperationType::Inversion: {
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    const float scale = float(1.0 - 2.0 * progress);
    matrix(0, 0) = scale;
    matrix(1, 1) = scale;
    matrix(2, 2) = scale;
    return matrix;
  }
  case MolecularSymmetryOperationType::Reflection:
    return reflection_matrix(axis, progress);
  case MolecularSymmetryOperationType::ProperRotation:
    return axis_angle_matrix(
        axis, 2.0 * kPi * double(shortest_signed_power(power, order)) * progress / double(order));
  case MolecularSymmetryOperationType::ImproperRotation: {
    const double rotation_progress = std::min(1.0, progress * 2.0);
    const double reflection_progress = std::max(0.0, progress * 2.0 - 1.0);
    return reflection_matrix(axis, reflection_progress)
        * axis_angle_matrix(axis,
                            2.0 * kPi * double(shortest_signed_power(power, order))
                                * rotation_progress / double(order));
  }
  }
  return identity;
}

bool MolecularSymmetryDefinition::is_valid() const {
  return geometry.is_valid() && !point_group.trimmed().isEmpty()
      && !operations.isEmpty() && operations.size() <= kMaximumOperations
      && operation_colors.is_valid()
      && animation_duration_ms >= 200 && animation_duration_ms <= 5000
      && std::all_of(operations.cbegin(), operations.cend(),
                     [](const auto& operation) { return operation.is_valid(); });
}

QString molecular_symmetry_operation_type_name(MolecularSymmetryOperationType type) {
  switch (type) {
  case MolecularSymmetryOperationType::Identity: return QStringLiteral("identity");
  case MolecularSymmetryOperationType::ProperRotation: return QStringLiteral("proper rotation");
  case MolecularSymmetryOperationType::Reflection: return QStringLiteral("reflection");
  case MolecularSymmetryOperationType::Inversion: return QStringLiteral("inversion");
  case MolecularSymmetryOperationType::ImproperRotation:
    return QStringLiteral("improper rotation");
  }
  return {};
}

bool parse_molecular_symmetry(const QByteArray& payload,
                              MolecularSymmetryDefinition* definition,
                              QString* error_message) {
  if (!definition) {
    set_error(error_message, QStringLiteral("Missing molecular-symmetry output object"));
    return false;
  }
  *definition = {};
  if (payload.isEmpty() || payload.size() > kMaximumPayloadBytes) {
    set_error(error_message,
              QStringLiteral("Molecular-symmetry payload is empty or exceeds 4 MiB"));
    return false;
  }

  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    set_error(error_message,
              QStringLiteral("Invalid molecular-symmetry JSON: %1").arg(parse_error.errorString()));
    return false;
  }
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("format")).toString() != QStringLiteral("uil.molecular-symmetry")
      || root.value(QStringLiteral("version")).toInt() != 1) {
    set_error(error_message, QStringLiteral("Expected uil.molecular-symmetry version 1"));
    return false;
  }

  MolecularSymmetryDefinition parsed;
  parsed.title = root.value(QStringLiteral("title")).toString().trimmed();
  parsed.point_group = root.value(QStringLiteral("point_group")).toString().trimmed();
  parsed.animation_duration_ms = std::clamp(
      root.value(QStringLiteral("animation_duration_ms")).toInt(1100), 200, 5000);
  if (root.contains(QStringLiteral("operation_colors"))
      && !root.value(QStringLiteral("operation_colors")).isObject()) {
    set_error(error_message, QStringLiteral("operation_colors must be an object"));
    return false;
  }
  const QJsonObject colors = root.value(QStringLiteral("operation_colors")).toObject();
  if (!parse_operation_color(colors, QStringLiteral("identity"),
                             &parsed.operation_colors.identity, error_message)
      || !parse_operation_color(colors, QStringLiteral("rotation"),
                                &parsed.operation_colors.rotation, error_message)
      || !parse_operation_color(colors, QStringLiteral("reflection"),
                                &parsed.operation_colors.reflection, error_message)
      || !parse_operation_color(colors, QStringLiteral("inversion"),
                                &parsed.operation_colors.inversion, error_message)
      || !parse_operation_color(colors, QStringLiteral("improper_rotation"),
                                &parsed.operation_colors.improper_rotation, error_message)) {
    return false;
  }
  const QJsonObject molecule = root.value(QStringLiteral("molecule")).toObject();
  const QByteArray xyz = molecule.value(QStringLiteral("xyz")).toString().toUtf8();
  QString geometry_error;
  if (!parse_xyz_molecule(xyz, &parsed.geometry, &geometry_error)) {
    set_error(error_message, QStringLiteral("Invalid embedded XYZ: %1").arg(geometry_error));
    return false;
  }

  const QJsonArray operations = root.value(QStringLiteral("operations")).toArray();
  if (operations.isEmpty() || operations.size() > kMaximumOperations) {
    set_error(error_message, QStringLiteral("Operations must contain between 1 and 96 entries"));
    return false;
  }
  parsed.operations.reserve(operations.size());
  for (int index = 0; index < operations.size(); ++index) {
    const QJsonObject object = operations.at(index).toObject();
    MolecularSymmetryOperation operation;
    operation.label = object.value(QStringLiteral("label")).toString().trimmed();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("identity")) {
      operation.type = MolecularSymmetryOperationType::Identity;
    } else if (type == QStringLiteral("rotation")) {
      operation.type = MolecularSymmetryOperationType::ProperRotation;
    } else if (type == QStringLiteral("reflection")) {
      operation.type = MolecularSymmetryOperationType::Reflection;
    } else if (type == QStringLiteral("inversion")) {
      operation.type = MolecularSymmetryOperationType::Inversion;
    } else if (type == QStringLiteral("improper-rotation")) {
      operation.type = MolecularSymmetryOperationType::ImproperRotation;
    } else {
      set_error(error_message,
                QStringLiteral("Operation %1 has an unknown type").arg(index + 1));
      return false;
    }
    operation.order = object.value(QStringLiteral("order")).toInt(1);
    operation.power = object.value(QStringLiteral("power")).toInt(1);
    if (operation.type == MolecularSymmetryOperationType::ProperRotation
        || operation.type == MolecularSymmetryOperationType::ImproperRotation
        || operation.type == MolecularSymmetryOperationType::Reflection) {
      if (!parse_axis(object.value(QStringLiteral("axis")), &operation.axis)) {
        set_error(error_message,
                  QStringLiteral("Operation %1 requires a finite three-component axis")
                      .arg(index + 1));
        return false;
      }
    }
    if (!operation.is_valid()) {
      set_error(error_message,
                QStringLiteral("Operation %1 has invalid parameters").arg(index + 1));
      return false;
    }
    if (!operation_maps_geometry(operation, parsed.geometry)) {
      set_error(error_message,
                QStringLiteral("Operation %1 (%2) does not map the molecule onto itself")
                    .arg(index + 1).arg(operation.label));
      return false;
    }
    parsed.operations.push_back(operation);
  }

  if (!parsed.is_valid()) {
    set_error(error_message, QStringLiteral("Incomplete molecular-symmetry definition"));
    return false;
  }
  *definition = std::move(parsed);
  if (error_message) error_message->clear();
  return true;
}
