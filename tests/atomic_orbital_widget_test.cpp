#include "ui/atomic_orbital_widget.hpp"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTest>

class AtomicOrbitalWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void renders_and_resamples_when_opengl_is_available();
    /** @brief Checks exact frame restoration without redundant geometry uploads and bounded
     * eviction. */
    void gpu_cache_reuses_and_evicts_geometry();
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

    auto* xy_button = widget.findChild<QPushButton*>(
        QStringLiteral("atomicOrbitalPlaneXYButton"));
    auto* xz_button = widget.findChild<QPushButton*>(
        QStringLiteral("atomicOrbitalPlaneXZButton"));
    auto* yz_button = widget.findChild<QPushButton*>(
        QStringLiteral("atomicOrbitalPlaneYZButton"));
    QVERIFY(xy_button);
    QVERIFY(xz_button);
    QVERIFY(yz_button);
    QVERIFY(yz_button->isChecked());

    slider->setValue(700);
    QVERIFY(!qFuzzyIsNull(widget.plane_offset()));
    QTest::mouseClick(xy_button, Qt::LeftButton);
    QCOMPARE(widget.definition().plane, AtomicOrbitalDefinition::Plane::XY);
    QVERIFY(xy_button->isChecked());
    QVERIFY(qFuzzyIsNull(widget.plane_offset()));
    slider->setValue(700);
    QTest::mouseClick(xz_button, Qt::LeftButton);
    QCOMPARE(widget.definition().plane, AtomicOrbitalDefinition::Plane::XZ);
    QVERIFY(xz_button->isChecked());
    QVERIFY(qFuzzyIsNull(widget.plane_offset()));

    // Rotation is a rigid transform of the orbital, axes, and sampling plane.
    // It changes the left-hand 3D scene but not the plane-relative contour image.
    slider->setValue(650);
    const QImage before_rotation = widget.capture_frame();
    const QPointF press_position(180.0, 180.0);
    const QPointF move_position(300.0, 225.0);
    QMouseEvent press_event(
        QEvent::MouseButtonPress, press_position,
        QPointF(widget.mapToGlobal(press_position.toPoint())),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press_event);
    QMouseEvent move_event(
        QEvent::MouseMove, move_position,
        QPointF(widget.mapToGlobal(move_position.toPoint())),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &move_event);
    QMouseEvent release_event(
        QEvent::MouseButtonRelease, move_position,
        QPointF(widget.mapToGlobal(move_position.toPoint())),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &release_event);
    const QImage after_rotation = widget.capture_frame();
    const QRect left_crop(
        before_rotation.width() * 3 / 100, before_rotation.height() * 15 / 100,
        before_rotation.width() * 38 / 100, before_rotation.height() * 48 / 100);
    const QRect right_crop(
        before_rotation.width() * 53 / 100, before_rotation.height() * 15 / 100,
        before_rotation.width() * 35 / 100, before_rotation.height() * 48 / 100);
    QVERIFY(before_rotation.copy(left_crop) != after_rotation.copy(left_crop));
    QCOMPARE(before_rotation.copy(right_crop), after_rotation.copy(right_crop));
}

void AtomicOrbitalWidgetTest::gpu_cache_reuses_and_evicts_geometry() {
  if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
    QSKIP("Requires QOpenGLWidget");
  QWidget new_host;
  new_host.resize(900, 480);
  AtomicOrbitalWidget widget;
  widget.resize(900, 480);
  AtomicOrbitalDefinition first;
  first.grid_size = 129;
  const auto first_volume = build_atomic_orbital_volume(first);
  widget.set_prepared_definition(first, first_volume);
  widget.show();
  QTest::qWait(100);
  if (!widget.isValid()) QSKIP("No OpenGL context");
  const auto original = widget.capture_frame();
  QVERIFY(widget.renderer_available());
  QCOMPARE(widget.geometry_upload_count(), quint64(1));
  auto second = first;
  second.n = 2;
  widget.set_definition(second);
  QVERIFY(widget.renderer_available());
  QCOMPARE(widget.geometry_upload_count(), quint64(2));
  widget.set_prepared_definition(first, first_volume);
  QCOMPARE(widget.geometry_upload_count(), quint64(2));
  QCOMPARE(widget.capture_frame(), original);
  auto themed = first;
  themed.colormap = "garnet_slate";
  widget.set_prepared_definition(themed, first_volume);
  QCOMPARE(widget.geometry_upload_count(), quint64(2));
  QVERIFY(widget.capture_frame() != original);
  // Each 129-cubed texture alone exceeds 8 MiB. Four inactive textures cannot
  // fit the 32 MiB budget, so scanning through five orbitals must evict 1s.
  for (int n = 2; n <= 5; ++n) {
    auto definition = first;
    definition.n = n;
    widget.set_definition(definition);
    QVERIFY(widget.renderer_available());
    QVERIFY(widget.gpu_cache_bytes() <= 32 * 1024 * 1024);
  }
  const auto uploads = widget.geometry_upload_count();
  widget.set_prepared_definition(first, first_volume);
  QCOMPARE(widget.geometry_upload_count(), uploads + 1);
  QCOMPARE(widget.capture_frame(), original);
  QVERIFY(widget.gpu_cache_bytes() <= 32 * 1024 * 1024);
  // Reparenting may recreate the GL context. Cached names must be destroyed in
  // their original context, and the current volume must be uploaded again safely.
  widget.setParent(&new_host);
  new_host.show();
  widget.show();
  QTRY_VERIFY(widget.renderer_available());
  const auto recreated = widget.capture_frame();
  QCOMPARE(recreated.size(), original.size());
  // Qt may choose different antialiasing for a child widget in another native
  // window. Check the whole image within a small average channel tolerance;
  // the same-context cache round trips above must remain pixel-identical.
  quint64 difference = 0;
  for (int y = 0; y < original.height(); ++y) {
    for (int x = 0; x < original.width(); ++x) {
      const QColor a = original.pixelColor(x, y);
      const QColor b = recreated.pixelColor(x, y);
      difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                    std::abs(a.blue() - b.blue());
    }
  }
  const double mean_error = double(difference) / (original.width() * original.height() * 3);
  QVERIFY2(mean_error < 2.0, qPrintable(QString("Mean channel error: %1").arg(mean_error)));
}

QTEST_MAIN(AtomicOrbitalWidgetTest)

#include "atomic_orbital_widget_test.moc"
