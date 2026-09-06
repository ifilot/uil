#include "ui/atomic_orbital_widget.hpp"

#include <QGuiApplication>
#include <QSlider>
#include <QTest>

class AtomicOrbitalWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void renders_and_resamples_when_opengl_is_available();
};

void AtomicOrbitalWidgetTest::renders_and_resamples_when_opengl_is_available() {
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        QSKIP("The offscreen Qt platform cannot safely expose QOpenGLWidget");
    }

    AtomicOrbitalDefinition definition;
    definition.title = QStringLiteral("Hydrogen-like $2p_z$ orbital");
    definition.orbital = QStringLiteral("2pz");
    definition.n = 2;
    definition.l = 1;
    definition.m = 0;
    definition.grid_size = 33;
    definition.plane = AtomicOrbitalDefinition::Plane::XZ;

    AtomicOrbitalWidget widget;
    widget.resize(900, 480);
    widget.set_definition(definition);
    widget.show();
    QTest::qWait(150);
    if (!widget.isValid()) {
        QSKIP("The active Qt platform does not provide a QOpenGLWidget context");
    }
    QVERIFY2(widget.renderer_available(), qPrintable(widget.renderer_error()));
    const QImage centered = widget.capture_frame();
    QVERIFY(!centered.isNull());

    auto* slider = widget.findChild<QSlider*>(
        QStringLiteral("atomicOrbitalOffsetSlider"));
    QVERIFY(slider);
    slider->setValue(700);
    QTest::qWait(50);
    const QImage offset = widget.capture_frame();
    QVERIFY(!offset.isNull());
    QVERIFY(offset != centered);
    const double saved_offset = widget.plane_offset();
    for (const bool surface_only : {false, true}) {
      const QImage poster = widget.capture_poster(surface_only);
      QVERIFY(!poster.isNull());
      QCOMPARE(poster.pixelColor(0, 0), QColor(Qt::white));
      QVERIFY(poster != offset);
      QVERIFY(slider->isVisible());
      QCOMPARE(widget.plane_offset(), saved_offset);
      QCOMPARE(widget.capture_frame(), offset);
    }
    // A presentation-only change reuses the shared mesh/volume buffers, but must
    // refresh palette and sampling-plane resources and still produce a valid frame.
    const auto prepared = build_atomic_orbital_volume(definition);
    widget.set_prepared_definition(definition, prepared);
    auto themed = definition;
    themed.colormap = QStringLiteral("garnet_slate");
    themed.positive_color = QColor("#9d2235");
    themed.negative_color = QColor("#536878");
    widget.set_prepared_definition(themed, prepared);
    const auto themed_frame = widget.capture_frame();
    QVERIFY(widget.renderer_available());
    QVERIFY(!themed_frame.isNull());
    QVERIFY(themed_frame != centered);
    auto other_plane = themed;
    other_plane.plane = AtomicOrbitalDefinition::Plane::YZ;
    widget.set_prepared_definition(other_plane, prepared);
    QVERIFY(widget.renderer_available());
    QVERIFY(widget.capture_frame() != themed_frame);
}

QTEST_MAIN(AtomicOrbitalWidgetTest)

#include "atomic_orbital_widget_test.moc"
