#include "molecule/molecule_geometry.hpp"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr qint64 kMaximumXyzBytes = 4LL * 1024LL * 1024LL;
constexpr int kMaximumAtoms = 4096;
constexpr int kMaximumAtomsForBondInference = 1500;

/** @brief Assigns an optional error message. */
void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

/** @brief Returns a normalized chemical element symbol or an empty string. */
QString normalized_element_symbol(QString symbol) {
    symbol = symbol.trimmed();
    if (symbol.isEmpty() || symbol.size() > 3) {
        return {};
    }

    symbol[0] = symbol.at(0).toUpper();
    for (qsizetype i = 1; i < symbol.size(); ++i) {
        symbol[i] = symbol.at(i).toLower();
    }

    static const QRegularExpression element_expression(QStringLiteral("^[A-Z][a-z]{0,2}$"));
    return element_expression.match(symbol).hasMatch() ? symbol : QString();
}

/** @brief Returns an approximate covalent radius in angstrom for bond inference. */
double covalent_radius(const QString& element) {
    static const QHash<QString, double> radii{
        {QStringLiteral("H"), 0.31},  {QStringLiteral("B"), 0.84},
        {QStringLiteral("C"), 0.76},  {QStringLiteral("N"), 0.71},
        {QStringLiteral("O"), 0.66},  {QStringLiteral("F"), 0.57},
        {QStringLiteral("Si"), 1.11}, {QStringLiteral("P"), 1.07},
        {QStringLiteral("S"), 1.05},  {QStringLiteral("Cl"), 1.02},
        {QStringLiteral("Fe"), 1.32}, {QStringLiteral("Cu"), 1.32},
        {QStringLiteral("Zn"), 1.22}, {QStringLiteral("Br"), 1.20},
        {QStringLiteral("I"), 1.39},
    };
    return radii.value(element, 0.85);
}

/** @brief Infers a simple undirected bond list from covalent radii. */
QVector<MoleculeBond> infer_bonds(const QVector<MoleculeAtom>& atoms) {
    QVector<MoleculeBond> bonds;
    if (atoms.size() > kMaximumAtomsForBondInference) {
        return bonds;
    }

    for (int first = 0; first < atoms.size(); ++first) {
        for (int second = first + 1; second < atoms.size(); ++second) {
            const double cutoff = 1.22 * (covalent_radius(atoms.at(first).element)
                                          + covalent_radius(atoms.at(second).element));
            const double distance_squared =
                double((atoms.at(first).position - atoms.at(second).position).lengthSquared());
            if (distance_squared > 0.01 && distance_squared <= cutoff * cutoff) {
                bonds.push_back(MoleculeBond{first, second});
            }
        }
    }
    return bonds;
}
}  // namespace

bool MoleculeGeometry::is_valid() const {
    return !atoms.isEmpty();
}

bool load_xyz_molecule(
    const QString& path,
    MoleculeGeometry* geometry,
    QString* error_message) {
    if (!geometry) {
        set_error(error_message, QStringLiteral("Missing molecule output object"));
        return false;
    }
    *geometry = {};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        set_error(
            error_message,
            QStringLiteral("Could not open XYZ geometry: %1").arg(file.errorString()));
        return false;
    }
    if (file.size() < 0 || file.size() > kMaximumXyzBytes) {
        set_error(error_message, QStringLiteral("XYZ geometry exceeds the 4 MiB size limit"));
        return false;
    }

    QTextStream stream(&file);
    bool count_ok = false;
    const int atom_count = stream.readLine().trimmed().toInt(&count_ok);
    if (!count_ok || atom_count <= 0 || atom_count > kMaximumAtoms) {
        set_error(
            error_message,
            QStringLiteral("XYZ atom count must be between 1 and %1").arg(kMaximumAtoms));
        return false;
    }

    MoleculeGeometry parsed;
    parsed.description = stream.readLine().trimmed();
    parsed.atoms.reserve(atom_count);
    for (int atom_index = 0; atom_index < atom_count; ++atom_index) {
        if (stream.atEnd()) {
            set_error(
                error_message,
                QStringLiteral("XYZ ended before atom %1 of %2")
                    .arg(atom_index + 1)
                    .arg(atom_count));
            return false;
        }

        const QStringList fields =
            stream.readLine().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 4) {
            set_error(
                error_message,
                QStringLiteral("Invalid XYZ atom record on line %1").arg(atom_index + 3));
            return false;
        }

        const QString element = normalized_element_symbol(fields.at(0));
        bool x_ok = false;
        bool y_ok = false;
        bool z_ok = false;
        const double x = fields.at(1).toDouble(&x_ok);
        const double y = fields.at(2).toDouble(&y_ok);
        const double z = fields.at(3).toDouble(&z_ok);
        if (element.isEmpty() || !x_ok || !y_ok || !z_ok || !std::isfinite(x)
            || !std::isfinite(y) || !std::isfinite(z) || std::abs(x) > 1.0e6
            || std::abs(y) > 1.0e6 || std::abs(z) > 1.0e6) {
            set_error(
                error_message,
                QStringLiteral("Invalid XYZ atom record on line %1").arg(atom_index + 3));
            return false;
        }

        parsed.atoms.push_back(
            MoleculeAtom{element, QVector3D(float(x), float(y), float(z))});
    }

    parsed.bonds = infer_bonds(parsed.atoms);
    *geometry = std::move(parsed);
    if (error_message) {
        error_message->clear();
    }
    return true;
}
