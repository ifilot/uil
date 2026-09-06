#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QVector>
#include <QVector3D>

inline constexpr double kAtomicOrbitalContourMinimum = 1.0e-8;

/** @brief One real hydrogen-like orbital exposed by the atomic-orbital object. */
struct AtomicOrbitalCatalogEntry {
    QString name;
    QString label;
    int n = 1;
    int l = 0;
    int m = 0;
};

/** @brief Configuration embedded in a version-one UIL atomic-orbital object. */
struct AtomicOrbitalDefinition {
    enum class Plane { XY, XZ, YZ };

    QString title = QStringLiteral("Atomic orbital");
    QString orbital = QStringLiteral("1s");
    int n = 1;
    int l = 0;
    int m = 0;
    Plane plane = Plane::XY;
    QColor positive_color = QColor(QStringLiteral("#2563eb"));
    QColor negative_color = QColor(QStringLiteral("#dc2626"));
    QString colormap = QStringLiteral("coolwarm");
    double isovalue = 0.0;
    double offset_min = -1.0;
    double offset_max = 1.0;
    double offset_initial = 0.0;
    // Absolute symmetric color limit; always stored as a power of ten.
    double contour_maximum = 1.0e-2;
    int contour_levels = 12;
    int grid_size = 65;

    /** @brief Returns whether all parsed values are safe and internally consistent. */
    bool is_valid() const;
};

/** @brief Interleaved geometry used by the OpenGL orbital renderer. */
struct AtomicOrbitalVertex {
    QVector3D position;
    QVector3D normal;
};

/** @brief Sampled wavefunction and its positive and negative isosurfaces. */
struct AtomicOrbitalVolume {
    QVector<float> values;
    QVector<AtomicOrbitalVertex> positive_vertices;
    QVector<AtomicOrbitalVertex> negative_vertices;
    int grid_size = 0;
    float half_extent = 0.0f;
    float maximum_absolute_value = 0.0f;
    float isovalue = 0.0f;

    /** @brief Returns whether the volume and at least one phase surface were constructed. */
    bool is_valid() const;
};

/** @brief Returns all real hydrogen-like orbitals from 1s through 5g. */
QVector<AtomicOrbitalCatalogEntry> atomic_orbital_catalog();

/** @brief Resolves a Managlyph-style orbital name to quantum numbers. */
bool resolve_atomic_orbital(
    const QString& name,
    AtomicOrbitalCatalogEntry* entry);

/** @brief Evaluates a real normalized hydrogen-like wavefunction in atomic units. */
double hydrogenic_atomic_orbital_value(
    int n,
    int l,
    int m,
    double x,
    double y,
    double z);

/** @brief Parses and validates a version-one embedded atomic-orbital JSON payload. */
bool parse_atomic_orbital(
    const QByteArray& payload,
    AtomicOrbitalDefinition* definition,
    QString* error_message = nullptr);

/** @brief Samples a wavefunction and constructs both signed isosurfaces. */
AtomicOrbitalVolume build_atomic_orbital_volume(
    const AtomicOrbitalDefinition& definition,
    QString* error_message = nullptr);

/** @brief Returns a 256-color lookup table for a curated divergent colormap. */
QVector<QColor> atomic_orbital_colormap(const QString& name);

/** @brief Returns whether a curated divergent colormap name is supported. */
bool is_supported_atomic_orbital_colormap(const QString& name);
