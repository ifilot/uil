#include "orbital/atomic_orbital.hpp"

#include <QTest>

#include <cmath>

class AtomicOrbitalTest final : public QObject {
    Q_OBJECT

private slots:
    void catalog_matches_managlyph_order_and_extends_through_5g();
    void hydrogenic_values_have_expected_nodes_and_orientation();
    void payload_parses_rendering_and_sampling_options();
    void invalid_orbital_and_colormap_are_rejected();
    /** @brief Checks themed phase colors, reversal, and payload acceptance. */
    void garnet_colormaps_match_surface_phases();
    void signed_isosurfaces_are_generated();
    void entire_catalog_generates_at_preview_resolution();
};

void AtomicOrbitalTest::catalog_matches_managlyph_order_and_extends_through_5g() {
    const QVector<AtomicOrbitalCatalogEntry> catalog = atomic_orbital_catalog();
    QCOMPARE(catalog.size(), 55);
    QCOMPARE(catalog.at(0).name, QStringLiteral("1s"));
    QCOMPARE(catalog.at(1).name, QStringLiteral("2s"));
    QCOMPARE(catalog.at(2).name, QStringLiteral("2px"));
    QCOMPARE(catalog.at(3).name, QStringLiteral("2py"));
    QCOMPARE(catalog.at(4).name, QStringLiteral("2pz"));
    QCOMPARE(catalog.at(13).name, QStringLiteral("3dz2"));
    QCOMPARE(catalog.at(29).name, QStringLiteral("4fx(x2-3y2)"));
    QCOMPARE(catalog.constLast().n, 5);
    QCOMPARE(catalog.constLast().l, 4);
    QCOMPARE(catalog.constLast().m, 4);
}

void AtomicOrbitalTest::hydrogenic_values_have_expected_nodes_and_orientation() {
    const double one_s_origin = hydrogenic_atomic_orbital_value(1, 0, 0, 0, 0, 0);
    QVERIFY(std::abs(one_s_origin - 1.0 / std::sqrt(3.14159265358979323846)) < 1.0e-12);

    const double two_s_inside = hydrogenic_atomic_orbital_value(2, 0, 0, 1, 0, 0);
    const double two_s_outside = hydrogenic_atomic_orbital_value(2, 0, 0, 3, 0, 0);
    QVERIFY(two_s_inside * two_s_outside < 0.0);

    const double positive_z = hydrogenic_atomic_orbital_value(2, 1, 0, 0, 0, 1);
    const double negative_z = hydrogenic_atomic_orbital_value(2, 1, 0, 0, 0, -1);
    QVERIFY(positive_z * negative_z < 0.0);
    QVERIFY(std::abs(positive_z + negative_z) < 1.0e-12);
}

void AtomicOrbitalTest::payload_parses_rendering_and_sampling_options() {
    const QByteArray payload = R"json({
      "format": "uil.atomic-orbital",
      "version": 1,
      "title": "2p z slice",
      "orbital": "2pz",
      "surface": {
        "isovalue": 0.0125,
        "positive_color": "#2244aa",
        "negative_color": "#bb3311",
        "grid_size": 65
      },
      "sampling_plane": {
        "type": "xz",
        "offset": { "min": -0.8, "max": 0.9, "value": 0.25 }
      },
      "contour": {
        "colormap": "RdBu_r",
        "maximum": 0.004,
        "levels": 14
      }
    })json";
    AtomicOrbitalDefinition definition;
    QString error;
    QVERIFY2(parse_atomic_orbital(payload, &definition, &error), qPrintable(error));
    QCOMPARE(definition.orbital, QStringLiteral("2pz"));
    QCOMPARE(definition.n, 2);
    QCOMPARE(definition.l, 1);
    QCOMPARE(definition.m, 0);
    QCOMPARE(definition.plane, AtomicOrbitalDefinition::Plane::XZ);
    QCOMPARE(definition.colormap, QStringLiteral("RdBu_r"));
    QCOMPARE(definition.contour_levels, 14);
    QCOMPARE(definition.contour_maximum, 0.01);
    QCOMPARE(definition.isovalue, 0.0125);
}

void AtomicOrbitalTest::invalid_orbital_and_colormap_are_rejected() {
    AtomicOrbitalDefinition definition;
    QString error;
    QVERIFY(!parse_atomic_orbital(
        QByteArrayLiteral(R"({"format":"uil.atomic-orbital","version":1,"orbital":"6h"})"),
        &definition, &error));
    QVERIFY(error.contains(QStringLiteral("Unsupported atomic orbital")));

    QVERIFY(!parse_atomic_orbital(
        QByteArrayLiteral(R"({
          "format":"uil.atomic-orbital","version":1,"orbital":"2s",
          "contour":{"colormap":"viridis"}
        })"),
        &definition, &error));
    QVERIFY(error.contains(QStringLiteral("invalid")));

    QVERIFY(!parse_atomic_orbital(
        QByteArrayLiteral(R"({
          "format":"uil.atomic-orbital","version":1,"orbital":"2s",
          "contour":{"maximum":1e-9}
        })"),
        &definition, &error));
    QVERIFY(error.contains(QStringLiteral("invalid")));
}

void AtomicOrbitalTest::garnet_colormaps_match_surface_phases() {
  for (const QString& name : {QStringLiteral("garnet_teal"), QStringLiteral("garnet_slate")}) {
    const auto colors = atomic_orbital_colormap(name);
    const auto reversed = atomic_orbital_colormap(name + QStringLiteral("_r"));
    QCOMPARE(colors.size(), 256);
    QCOMPARE(reversed.size(), colors.size());
    QCOMPARE(colors.constFirst(),
             QColor(name == QStringLiteral("garnet_teal") ? "#177e72" : "#426b86"));
    QCOMPARE(colors.constLast(), QColor("#8e1b3e"));
    for (int index = 0; index < colors.size(); ++index) {
      QCOMPARE(colors.at(index), reversed.at(255 - index));
    }
    QVERIFY(colors.at(127).lightnessF() > 0.95);
    QVERIFY(colors.at(128).lightnessF() > 0.95);
    for (const QString& variant : {name, name + QStringLiteral("_r")}) {
      AtomicOrbitalDefinition definition;
      QString error;
      const QByteArray payload =
          QStringLiteral(
              "{\"format\":\"uil.atomic-orbital\",\"version\":1,\"orbital\":\"2s\","
              "\"contour\":{\"colormap\":\"%1\"}}")
              .arg(variant)
              .toUtf8();
      QVERIFY2(parse_atomic_orbital(payload, &definition, &error), qPrintable(error));
      QCOMPARE(definition.colormap, variant);
    }
  }
}

void AtomicOrbitalTest::signed_isosurfaces_are_generated() {
    AtomicOrbitalDefinition definition;
    definition.title = QStringLiteral("2s");
    definition.orbital = QStringLiteral("2s");
    definition.n = 2;
    definition.l = 0;
    definition.m = 0;
    definition.grid_size = 33;
    QString error;
    const AtomicOrbitalVolume volume = build_atomic_orbital_volume(definition, &error);
    QVERIFY2(volume.is_valid(), qPrintable(error));
    QVERIFY(!volume.positive_vertices.isEmpty());
    QVERIFY(!volume.negative_vertices.isEmpty());
    QVERIFY(volume.maximum_absolute_value > volume.isovalue);
}

void AtomicOrbitalTest::entire_catalog_generates_at_preview_resolution() {
    for (const AtomicOrbitalCatalogEntry& entry : atomic_orbital_catalog()) {
        AtomicOrbitalDefinition definition;
        definition.title = entry.name;
        definition.orbital = entry.name;
        definition.n = entry.n;
        definition.l = entry.l;
        definition.m = entry.m;
        definition.grid_size = 33;
        QString error;
        const AtomicOrbitalVolume volume =
            build_atomic_orbital_volume(definition, &error);
        QVERIFY2(volume.is_valid(),
                 qPrintable(QStringLiteral("%1: %2").arg(entry.name, error)));
    }
}

QTEST_GUILESS_MAIN(AtomicOrbitalTest)

#include "atomic_orbital_test.moc"
