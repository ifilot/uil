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
}

QTEST_MAIN(AtomicOrbitalWidgetTest)

#include "atomic_orbital_widget_test.moc"
