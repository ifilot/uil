#include "ui/molecule_widget.hpp"

#include <QDir>
#include <QFrame>
#include <QGuiApplication>
#include <QTest>
#include <QTimer>
#include <QToolButton>

class MoleculeWidgetTest final : public QObject {
  Q_OBJECT

 private slots:
  /** @brief Verifies mode APIs and the collapsible toolbar state. */
  void visualizer_controls_follow_public_state();
  /** @brief Verifies that vibration playback is available only with displacement data. */
  void vibration_control_tracks_geometry_capability();
  /** @brief Smoke-tests each rendering mode when an OpenGL display is available. */
  void renders_supported_modes_when_opengl_is_available();
  /** @brief Exercises every baked basis and checks a fixed-center p phase reversal. */
  void renders_baked_orbitals();
};

void MoleculeWidgetTest::visualizer_controls_follow_public_state() {
  MoleculeWidget widget;
  QCOMPARE(widget.stereo_mode(), MoleculeWidget::StereoMode::Mono);
  QVERIFY(widget.axes_visible());
  QVERIFY(!widget.world_axes_visible());
  widget.set_world_axes_visible(true);
  QVERIFY(widget.world_axes_visible());
  QVERIFY(widget.toolbar_expanded());
  widget.set_default_camera_distance_factor(2.0f);
  QCOMPARE(widget.camera_distance_factor(), 2.0f);
  QVERIFY(widget.findChild<QToolButton*>(QStringLiteral("moleculeStereoButton")));
  QVERIFY(widget.findChild<QToolButton*>(QStringLiteral("moleculeAxesButton")));
  QVERIFY(widget.findChild<QToolButton*>(QStringLiteral("moleculeAutoRotationButton")));
  QVERIFY(widget.findChild<QToolButton*>(QStringLiteral("moleculeResetButton")));

  QPoint context_menu_position;
  widget.set_context_menu_handler([&context_menu_position](const QPoint& global_position) {
    context_menu_position = global_position;
  });
  QTest::mouseClick(&widget, Qt::RightButton, Qt::NoModifier, QPoint(20, 20));
  QVERIFY(!context_menu_position.isNull());

  QToolButton* stereo_button =
      widget.findChild<QToolButton*>(QStringLiteral("moleculeStereoButton"));
  QVERIFY(stereo_button);
  stereo_button->click();
  QCOMPARE(widget.stereo_mode(), MoleculeWidget::StereoMode::RedCyanAnaglyph);
  QVERIFY(stereo_button->isChecked());
  stereo_button->click();
  QCOMPARE(widget.stereo_mode(), MoleculeWidget::StereoMode::Mono);
  QVERIFY(!stereo_button->isChecked());

  QToolButton* rotation_button =
      widget.findChild<QToolButton*>(QStringLiteral("moleculeAutoRotationButton"));
  QTimer* rotation_timer = widget.findChild<QTimer*>(QStringLiteral("moleculeAutoRotationTimer"));
  QVERIFY(rotation_button);
  QVERIFY(rotation_timer);
  QVERIFY(!widget.auto_rotation_enabled());
  rotation_button->click();
  QVERIFY(widget.auto_rotation_enabled());
  QVERIFY(rotation_button->isChecked());
  if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
    widget.show();
    QTRY_VERIFY(rotation_timer->isActive());
    widget.hide();
    QVERIFY(widget.auto_rotation_enabled());
    QVERIFY(!rotation_timer->isActive());
    widget.show();
    QTRY_VERIFY(rotation_timer->isActive());
  } else {
    QVERIFY(!rotation_timer->isActive());
  }
  rotation_button->click();
  QVERIFY(!widget.auto_rotation_enabled());
  QVERIFY(!rotation_timer->isActive());

  widget.set_axes_visible(false);
  QVERIFY(!widget.axes_visible());
  widget.set_toolbar_expanded(false);
  QVERIFY(!widget.toolbar_expanded());
  QVERIFY(widget.findChild<QFrame*>(QStringLiteral("moleculeToolbarPanel"))->isHidden());
  widget.set_toolbar_expanded(true);
  QVERIFY(widget.toolbar_expanded());

  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::MirrorPlane,
                              QVector3D(0.0f, 2.0f, 0.0f), QColor(QStringLiteral("#277d83")));
  QCOMPARE(widget.symmetry_element(), MoleculeWidget::SymmetryElement::MirrorPlane);
  QCOMPARE(widget.symmetry_element_axis(), QVector3D(0.0f, 1.0f, 0.0f));
  QCOMPARE(widget.symmetry_element_color(), QColor(QStringLiteral("#277d83")));
  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::None);
  QCOMPARE(widget.symmetry_element(), MoleculeWidget::SymmetryElement::None);
  widget.set_coordinate_origin(QVector3D(0.25f, -0.5f, 0.75f));
  QCOMPARE(widget.coordinate_origin(), QVector3D(0.25f, -0.5f, 0.75f));
  QVERIFY(!widget.reference_geometry_visible());
  QCOMPARE(widget.reference_geometry_opacity(), 0.5f);
  widget.set_reference_geometry_visible(true);
  QVERIFY(widget.reference_geometry_visible());
  widget.set_reference_geometry_opacity(0.35f);
  QCOMPARE(widget.reference_geometry_opacity(), 0.35f);
  widget.set_reference_geometry_opacity(2.0f);
  QCOMPARE(widget.reference_geometry_opacity(), 1.0f);
}

void MoleculeWidgetTest::vibration_control_tracks_geometry_capability() {
  MoleculeWidget widget;
  QToolButton* vibration_button =
      widget.findChild<QToolButton*>(QStringLiteral("moleculeVibrationButton"));
  QVERIFY(vibration_button);
  QVERIFY(!vibration_button->isEnabled());

  MoleculeGeometry static_geometry;
  static_geometry.atoms.push_back(MoleculeAtom{QStringLiteral("H"), QVector3D(), QVector3D()});
  widget.set_geometry(static_geometry);
  widget.set_vibration_playing(true);
  QVERIFY(!widget.vibration_playing());
  QVERIFY(!vibration_button->isEnabled());

  MoleculeGeometry animated_geometry = static_geometry;
  animated_geometry.atoms[0].vibration = QVector3D(0.1f, 0.0f, 0.0f);
  widget.set_geometry(animated_geometry);
  QVERIFY(vibration_button->isEnabled());
  widget.set_vibration_playing(true);
  QVERIFY(widget.vibration_playing());
  widget.set_vibration_playing(false);
  QVERIFY(!widget.vibration_playing());
}

void MoleculeWidgetTest::renders_supported_modes_when_opengl_is_available() {
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
    QSKIP("The offscreen Qt platform cannot safely expose QOpenGLWidget");
  }
  MoleculeWidget widget;
  widget.resize(640, 360);
  MoleculeGeometry geometry;
  geometry.atoms = {
      MoleculeAtom{QStringLiteral("O"), QVector3D(), QVector3D(0.0f, 0.1f, 0.0f)},
      MoleculeAtom{QStringLiteral("H"), QVector3D(0.95f, 0.0f, 0.0f), QVector3D(0.0f, -0.1f, 0.0f)},
  };
  geometry.bonds = {MoleculeBond{0, 1}};
  widget.set_geometry(geometry);
  widget.show();
  QTest::qWait(100);
  if (!widget.isValid()) {
    QSKIP("The active Qt platform does not provide a QOpenGLWidget context");
  }

  const QImage mono = widget.grabFramebuffer();
  QVERIFY(!mono.isNull());
  QCOMPARE(mono.size(), widget.size() * widget.devicePixelRatioF());

  widget.update();
  QTest::qWait(50);
  QCOMPARE(widget.grabFramebuffer(), mono);

  widget.set_stereo_mode(MoleculeWidget::StereoMode::RedCyanAnaglyph);
  QTest::qWait(50);
  const QImage anaglyph = widget.grabFramebuffer();
  QVERIFY(!anaglyph.isNull());
  QVERIFY(anaglyph != mono);

  widget.set_stereo_mode(MoleculeWidget::StereoMode::Mono);
  const QImage before_rotation = widget.grabFramebuffer();
  QMatrix4x4 operation_transform;
  operation_transform.rotate(65.0f, QVector3D(0.0f, 0.0f, 1.0f));
  widget.set_coordinate_transform(operation_transform);
  QTest::qWait(50);
  const QImage transformed_without_reference = widget.grabFramebuffer();
  widget.set_reference_geometry_visible(true);
  QTest::qWait(50);
  QVERIFY(widget.grabFramebuffer() != transformed_without_reference);
  widget.set_reference_geometry_visible(false);
  widget.clear_coordinate_transform();
  // All atom centers lie in the xy plane. Reflection must still be visible
  // through the atom surfaces, and both endpoints must recover the original.
  const QImage reflection_start = widget.grabFramebuffer();
  widget.set_atom_reflection_shape(QVector3D(0.0f, 0.0f, 1.0f), 0.5);
  QVERIFY(widget.grabFramebuffer() != reflection_start);
  widget.set_atom_reflection_shape(QVector3D(0.0f, 0.0f, 1.0f), 1.0);
  QCOMPARE(widget.grabFramebuffer(), reflection_start);
  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::RotationAxis,
                              QVector3D(0.0f, 0.0f, 1.0f));
  QTest::qWait(50);
  const QImage with_rotation_axis = widget.grabFramebuffer();
  QVERIFY(with_rotation_axis != before_rotation);
  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::MirrorPlane,
                              QVector3D(0.0f, 1.0f, 0.0f));
  QTest::qWait(50);
  const QImage with_mirror_plane = widget.grabFramebuffer();
  QVERIFY(with_mirror_plane != with_rotation_axis);
  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::InversionCenter);
  QTest::qWait(50);
  const QImage with_inversion_center = widget.grabFramebuffer();
  QVERIFY(with_inversion_center != with_mirror_plane);
  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::ImproperAxisAndPlane,
                              QVector3D(0.0f, 0.0f, 1.0f));
  QTest::qWait(50);
  QVERIFY(widget.grabFramebuffer() != with_inversion_center);
  widget.set_symmetry_element(MoleculeWidget::SymmetryElement::None);
  widget.set_auto_rotation_enabled(true);
  QTest::qWait(180);
  const QImage after_rotation = widget.grabFramebuffer();
  QVERIFY(!after_rotation.isNull());
  QVERIFY(after_rotation != before_rotation);
  widget.set_auto_rotation_enabled(false);
}

QTEST_MAIN(MoleculeWidgetTest)

void MoleculeWidgetTest::renders_baked_orbitals() {
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) QSKIP("OpenGL required");
  MoleculeWidget widget;
  widget.resize(640, 480);
  MoleculeGeometry geometry;
  geometry.atoms = {{QStringLiteral("O"), {}, {}}};
  widget.set_geometry(geometry);
  widget.set_builtin_controls_visible(false);
  widget.set_axes_visible(false);
  widget.set_default_view_rotation(QQuaternion::fromEulerAngles(-20, 30, 20));
  widget.show();
  QTest::qWait(100);
  if (!widget.isValid()) QSKIP("OpenGL context unavailable");
  const QImage bare = widget.grabFramebuffer();
  const QString output = qEnvironmentVariable("UIL_ORBITAL_TEST_IMAGES");
  if (!output.isEmpty()) QVERIFY(QDir().mkpath(output));
  for (const auto& name : symmetry_orbital_names()) {
    widget.set_orbitals({{1, name, 1.0f}});
    const QImage rendered = widget.grabFramebuffer();
    QVERIFY(rendered != bare);
    if (!output.isEmpty()) QVERIFY(rendered.save(QDir(output).filePath(name + ".png")));
  }
  widget.set_orbitals({{1, "2px", 1.0f}});
  const QImage original = widget.grabFramebuffer();
  QMatrix4x4 reflection;
  reflection.scale(-1.0f, 1.0f, 1.0f);
  widget.set_coordinate_transform(reflection);
  const QImage reflected = widget.grabFramebuffer();
  int phase_swaps = 0;
  for (int y = 0; y < original.height(); ++y) {
    for (int x = 0; x < original.width(); ++x) {
      const QColor before = original.pixelColor(x, y);
      const QColor after = reflected.pixelColor(x, y);
      if (before.green() > before.red() + 30 && after.red() > after.green() + 20 &&
          before.blue() > before.red() + 30 && after.blue() > after.green() + 30) ++phase_swaps;
    }
  }
  QVERIFY(phase_swaps > 100);
  widget.clear_coordinate_transform();
  if (!output.isEmpty()) {
    original.save(QDir(output).filePath("phase-original.png"));
    widget.grabFramebuffer().save(QDir(output).filePath("phase-reset.png"));
  }
  const QImage reset = widget.grabFramebuffer();
  QCOMPARE(reset.size(), original.size());
  // Alpha blending can round a few channels by one 8-bit step after a reflected
  // normal matrix. Reject geometric/color changes, not one-LSB GPU rounding.
  int rounded_pixels = 0;
  for (int y = 0; y < original.height(); ++y) {
    for (int x = 0; x < original.width(); ++x) {
      const QColor a = original.pixelColor(x, y), b = reset.pixelColor(x, y);
      QVERIFY(std::abs(a.red() - b.red()) <= 1);
      QVERIFY(std::abs(a.green() - b.green()) <= 1);
      QVERIFY(std::abs(a.blue() - b.blue()) <= 1);
      QCOMPARE(b.alpha(), 255);
      if (a != b) ++rounded_pixels;
    }
  }
  QVERIFY(rounded_pixels < original.width() * original.height() / 1000);
}

#include "molecule_widget_test.moc"
