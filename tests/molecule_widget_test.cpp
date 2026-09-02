#include "ui/molecule_widget.hpp"

#include <QFrame>
#include <QGuiApplication>
#include <QTest>
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
};

void MoleculeWidgetTest::visualizer_controls_follow_public_state() {
  MoleculeWidget widget;
  QCOMPARE(widget.stereo_mode(), MoleculeWidget::StereoMode::Mono);
  QVERIFY(widget.axes_visible());
  QVERIFY(widget.toolbar_expanded());
  QVERIFY(widget.findChild<QToolButton*>(QStringLiteral("moleculeStereoButton")));
  QVERIFY(widget.findChild<QToolButton*>(QStringLiteral("moleculeAxesButton")));
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

  widget.set_axes_visible(false);
  QVERIFY(!widget.axes_visible());
  widget.set_toolbar_expanded(false);
  QVERIFY(!widget.toolbar_expanded());
  QVERIFY(widget.findChild<QFrame*>(QStringLiteral("moleculeToolbarPanel"))->isHidden());
  widget.set_toolbar_expanded(true);
  QVERIFY(widget.toolbar_expanded());
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
}

QTEST_MAIN(MoleculeWidgetTest)

#include "molecule_widget_test.moc"
