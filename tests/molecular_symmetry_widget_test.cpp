#include "ui/molecular_symmetry_widget.hpp"
#include "ui/molecule_widget.hpp"

#include <QGuiApplication>
#include <QPushButton>
#include <QTest>

class MolecularSymmetryWidgetTest final : public QObject {
  Q_OBJECT

 private slots:
  void exposes_and_plays_every_operation_when_opengl_is_available();
};

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
  const auto buttons = widget.findChildren<QPushButton*>(
      QStringLiteral("molecularSymmetryOperationButton"));
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
  QTest::qWait(130);
  QVERIFY(widget.capture_frame() != initial);
  QTRY_VERIFY_WITH_TIMEOUT(!widget.is_animating(), 800);
  QCOMPARE(widget.active_operation_index(), -1);
  QCOMPARE(widget.selected_operation_index(), 1);
  QVERIFY(rotation_button->isChecked());
  auto* molecule = dynamic_cast<MoleculeWidget*>(widget.findChild<QWidget*>(
      QStringLiteral("symmetryMoleculeOpenGLWidget")));
  QVERIFY(molecule);
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
}

QTEST_MAIN(MolecularSymmetryWidgetTest)

#include "molecular_symmetry_widget_test.moc"
