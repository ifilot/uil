#include "ui/molecular_symmetry_widget.hpp"

#include <QCheckBox>
#include <QDir>
#include <QGuiApplication>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include "ui/molecule_widget.hpp"

class MolecularSymmetryWidgetTest final : public QObject {
  Q_OBJECT

 private slots:
  void filters_supported_ground_state_subshells();
  void exposes_and_plays_every_operation_when_opengl_is_available();
};

void MolecularSymmetryWidgetTest::filters_supported_ground_state_subshells() {
  QCOMPARE(default_symmetry_orbital_radius("H"), 0.45f);
  QCOMPARE(default_symmetry_orbital_radius("C"), 0.85f);
  QCOMPARE(default_symmetry_orbital_radius("O"), 0.85f);
  QCOMPARE(occupied_symmetry_orbital_names("H"), QStringList({"1s"}));
  QCOMPARE(occupied_symmetry_orbital_names("He"), QStringList({"1s"}));
  QCOMPARE(occupied_symmetry_orbital_names("Li"), QStringList({"1s", "2s"}));
  QCOMPARE(occupied_symmetry_orbital_names("C"), QStringList({"1s", "2s", "2px", "2py", "2pz"}));
  QVERIFY(!occupied_symmetry_orbital_names("Ca").contains("3dxy"));
  QCOMPARE(occupied_symmetry_orbital_names("Sc"), symmetry_orbital_names());
  QCOMPARE(occupied_symmetry_orbital_names("Fe"), symmetry_orbital_names());
  QVERIFY(occupied_symmetry_orbital_names("Unknown").isEmpty());
}

void MolecularSymmetryWidgetTest::exposes_and_plays_every_operation_when_opengl_is_available() {
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
    QSKIP("The offscreen Qt platform cannot safely expose QOpenGLWidget");
  }
  const QByteArray payload = QByteArrayLiteral(R"json({
    "format":"uil.molecular-symmetry", "version":1,
    "title":"Water", "point_group":"C2v", "animation_duration_ms":300,
    "operation_colors":{"identity":"#112233","rotation":"#9b2f4f","reflection":"#277d83"},
    "molecule":{"xyz":"3\nWater\nO 0 0 0\nH 0 0.757 0.58605\nH 0 -0.757 0.58605\n"},
    "operations":[
      {"label":"E", "type":"identity"},
      {"label":"C2(z)", "type":"rotation", "order":2, "axis":[0,0,1]},
      {"label":"sigma-v(xz)", "type":"reflection", "axis":[0,1,0]},
      {"label":"sigma-v(yz)", "type":"reflection", "axis":[1,0,0]}
    ]
  })json");
  MolecularSymmetryDefinition definition;
  QString error;
  QVERIFY2(parse_molecular_symmetry(payload, &definition, &error), qPrintable(error));

  MolecularSymmetryWidget widget;
  widget.resize(900, 500);
  widget.set_definition(definition);
  widget.show();
  QTest::qWait(150);
  const QImage initial = widget.capture_frame();
  QVERIFY(!initial.isNull());
  auto* tabs = widget.findChild<QTabWidget*>(QStringLiteral("symmetryControlTabs"));
  QVERIFY(tabs);
  QCOMPARE(tabs->count(), 2);
  QCOMPARE(tabs->tabText(1), QStringLiteral("Settings"));
  const auto buttons =
      widget.findChildren<QPushButton*>(QStringLiteral("molecularSymmetryOperationButton"));
  QCOMPARE(buttons.size(), definition.operations.size());
  for (QPushButton* button : buttons) QCOMPARE(button->focusPolicy(), Qt::NoFocus);
  QVERIFY(buttons.at(0)->styleSheet().contains(QStringLiteral("#112233")));

  QPushButton* rotation_button = nullptr;
  for (QPushButton* button : buttons) {
    if (button->property("operationIndex").toInt() == 1) rotation_button = button;
  }
  QVERIFY(rotation_button);
  QTest::mouseClick(rotation_button, Qt::LeftButton);
  QCOMPARE(widget.active_operation_index(), 1);
  QVERIFY(widget.is_animating());
  auto* molecule = dynamic_cast<MoleculeWidget*>(
      widget.findChild<QWidget*>(QStringLiteral("symmetryMoleculeOpenGLWidget")));
  QVERIFY(molecule);
  auto* spin_button = widget.findChild<QPushButton*>(QStringLiteral("molecularSymmetrySpinButton"));
  QVERIFY(spin_button);
  QVERIFY(!spin_button->isChecked());
  QTest::mouseClick(spin_button, Qt::LeftButton);
  QVERIFY(molecule->auto_rotation_enabled());
  QVERIFY(widget.is_animating());
  const QPoint start = molecule->rect().center();
  QTest::mousePress(molecule, Qt::LeftButton, Qt::NoModifier, start);
  QMouseEvent drag(QEvent::MouseMove, QPointF(start + QPoint(40, 20)),
                   QPointF(molecule->mapToGlobal(start + QPoint(40, 20))), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(molecule, &drag);
  QTest::mouseRelease(molecule, Qt::LeftButton, Qt::NoModifier, start + QPoint(40, 20));
  QVERIFY(widget.is_animating());
  QVERIFY(molecule->reference_geometry_visible());
  QCOMPARE(molecule->reference_geometry_opacity(), 0.5f);
  QTest::qWait(130);
  QVERIFY(widget.capture_frame() != initial);
  QTRY_VERIFY_WITH_TIMEOUT(!widget.is_animating(), 800);
  QCOMPARE(widget.active_operation_index(), -1);
  QCOMPARE(widget.selected_operation_index(), 1);
  QVERIFY(rotation_button->isChecked());
  QVERIFY(!molecule->reference_geometry_visible());
  QVERIFY(molecule->auto_rotation_enabled());
  const QImage spinning = molecule->grabFramebuffer();
  QTest::qWait(100);
  QVERIFY(molecule->grabFramebuffer() != spinning);
  QTest::mouseClick(spin_button, Qt::LeftButton);
  QVERIFY(!molecule->auto_rotation_enabled());
  const QImage stopped = molecule->grabFramebuffer();
  QTest::qWait(80);
  QCOMPARE(molecule->grabFramebuffer(), stopped);
  QCOMPARE(molecule->coordinate_origin(), QVector3D());
  QCOMPARE(molecule->symmetry_element(), MoleculeWidget::SymmetryElement::RotationAxis);
  QCOMPARE(molecule->symmetry_element_color(), QColor(QStringLiteral("#9b2f4f")));
  QCOMPARE(molecule->camera_distance_factor(), 2.0f);

  QPushButton* reflection_button = nullptr;
  for (QPushButton* button : buttons) {
    if (button->property("operationIndex").toInt() == 2) reflection_button = button;
  }
  QVERIFY(reflection_button);
  QTest::mouseClick(reflection_button, Qt::LeftButton);
  QCOMPARE(widget.selected_operation_index(), 2);
  QCOMPARE(molecule->symmetry_element(), MoleculeWidget::SymmetryElement::MirrorPlane);
  QCOMPARE(molecule->symmetry_element_color(), QColor(QStringLiteral("#277d83")));
  auto* orbital_check = widget.findChild<QCheckBox*>(QStringLiteral("symmetryOrbital_2px"));
  auto* atom_selector = widget.findChild<QListWidget*>(QStringLiteral("symmetryOrbitalAtom"));
  QVERIFY(orbital_check);
  QVERIFY(atom_selector);
  QCOMPARE(atom_selector->count(), 3);
  orbital_check->click();
  QCOMPARE(molecule->orbitals().size(), 1);
  QCOMPARE(molecule->orbitals().first().atom, 1);
  tabs->setCurrentIndex(1);
  auto* d = widget.findChild<QCheckBox*>(QStringLiteral("symmetryOrbital_3dxy"));
  QVERIFY(d->isHidden());
  atom_selector->setCurrentRow(1);
  QVERIFY(!orbital_check->isChecked());
  QVERIFY(orbital_check->isHidden());
  auto* hydrogen_s = widget.findChild<QCheckBox*>(QStringLiteral("symmetryOrbital_1s"));
  hydrogen_s->click();
  QCOMPARE(molecule->orbitals().size(), 2);
  QCOMPARE(molecule->orbitals().last().atom, 2);
  QCOMPARE(molecule->orbitals().last().scale, 0.45f);
  hydrogen_s->click();
  atom_selector->setCurrentRow(0);
  QVERIFY(orbital_check->isChecked());
  auto* advanced = widget.findChild<QCheckBox*>(QStringLiteral("symmetryOrbitalAdvanced"));
  advanced->click();
  QVERIFY(!d->isHidden());
  d->click();
  QCOMPARE(molecule->orbitals().size(), 2);
  d->click();
  advanced->click();
  QVERIFY(d->isHidden());
  if (!qEnvironmentVariable("UIL_ORBITAL_TEST_IMAGES").isEmpty()) {
    QVERIFY(widget.grab().save(
        QDir(qEnvironmentVariable("UIL_ORBITAL_TEST_IMAGES")).filePath("orbital-settings.png")));
  }
  tabs->setCurrentIndex(0);
  widget.play_operation(3);
  QTRY_VERIFY_WITH_TIMEOUT(!widget.is_animating(), 1500);
  QVERIFY(molecule->reference_geometry_visible());
  const QImage reflected_orbital = molecule->grabFramebuffer();
  auto* camera_reset = widget.findChild<QPushButton*>(QStringLiteral("symmetryResetCamera"));
  QVERIFY(camera_reset);
  QTest::mouseClick(camera_reset, Qt::LeftButton);
  const QImage default_view = molecule->grabFramebuffer();
  const QPoint center = molecule->rect().center();
  QTest::mousePress(molecule, Qt::LeftButton, Qt::NoModifier, center);
  QMouseEvent camera_drag(QEvent::MouseMove, QPointF(center + QPoint(60, 30)),
                          QPointF(molecule->mapToGlobal(center + QPoint(60, 30))),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(molecule, &camera_drag);
  QTest::mouseRelease(molecule, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 30));
  QWheelEvent zoom(QPointF(center), QPointF(molecule->mapToGlobal(center)), QPoint(), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(molecule, &zoom);
  QVERIFY(molecule->camera_distance_factor() < 2.0f);
  QVERIFY(molecule->grabFramebuffer() != default_view);
  QTest::mouseClick(camera_reset, Qt::LeftButton);
  QCOMPARE(molecule->camera_distance_factor(), 2.0f);
  QCOMPARE(molecule->grabFramebuffer(), default_view);
  QCOMPARE(widget.selected_operation_index(), 3);
  QVERIFY(molecule->reference_geometry_visible());
  QCOMPARE(molecule->orbitals().size(), 1);
  auto* reset = widget.findChild<QPushButton*>(QStringLiteral("symmetryResetOperation"));
  QVERIFY(reset);
  reset->click();
  QVERIFY(!molecule->reference_geometry_visible());
  QVERIFY(molecule->grabFramebuffer() != reflected_orbital);
  QCOMPARE(molecule->orbitals().size(), 1);
}

QTEST_MAIN(MolecularSymmetryWidgetTest)

#include "molecular_symmetry_widget_test.moc"
