#include "molecule/molecule_camera.hpp"

#include <QtTest>

class MoleculeCameraTest final : public QObject {
  Q_OBJECT

 private slots:
  void fit_distance_contains_centered_sphere();
  void wheel_zoom_moves_camera_and_resizes_geometry();
  void perspective_makes_nearer_geometry_larger();
  void trackpad_zoom_and_limits_are_supported();
  void stereo_eye_separation_tracks_camera_distance();
  void default_view_uses_negative_x_with_positive_z_up();
};

void MoleculeCameraTest::fit_distance_contains_centered_sphere() {
  const float radius = 2.0f;
  const float landscape_distance = molecule_camera::fit_distance(radius, 16.0f / 9.0f);
  const float portrait_distance = molecule_camera::fit_distance(radius, 9.0f / 16.0f);

  QVERIFY(landscape_distance > radius);
  QVERIFY(portrait_distance > landscape_distance);
}

void MoleculeCameraTest::wheel_zoom_moves_camera_and_resizes_geometry() {
  const float radius = 2.0f;
  const float aspect = 16.0f / 9.0f;
  const float initial_distance = molecule_camera::distance(radius, aspect, 1.0f);
  const float zoom_factor = molecule_camera::zoomed_distance_factor(1.0f, 120, 0);
  const float zoomed_distance = molecule_camera::distance(radius, aspect, zoom_factor);
  const float initial_atom_radius =
      molecule_camera::projected_radius(0.4f, 0.0f, initial_distance, 600.0f);
  const float zoomed_atom_radius =
      molecule_camera::projected_radius(0.4f, 0.0f, zoomed_distance, 600.0f);

  QVERIFY(zoom_factor < 1.0f);
  QVERIFY(zoomed_distance < initial_distance);
  QVERIFY(zoomed_atom_radius > initial_atom_radius);
}

void MoleculeCameraTest::perspective_makes_nearer_geometry_larger() {
  const float camera_distance = 8.0f;
  const float far_radius = molecule_camera::projected_radius(0.4f, -1.0f, camera_distance, 600.0f);
  const float near_radius = molecule_camera::projected_radius(0.4f, 1.0f, camera_distance, 600.0f);

  QVERIFY(near_radius > far_radius);
}

void MoleculeCameraTest::trackpad_zoom_and_limits_are_supported() {
  const float trackpad_factor = molecule_camera::zoomed_distance_factor(1.0f, 0, 80);
  const float minimum = molecule_camera::zoomed_distance_factor(0.31f, 1200, 0);
  const float maximum = molecule_camera::zoomed_distance_factor(2.9f, -1200, 0);

  QVERIFY(trackpad_factor < 1.0f);
  QCOMPARE(minimum, molecule_camera::kMinimumDistanceFactor);
  QCOMPARE(maximum, molecule_camera::kMaximumDistanceFactor);
}

void MoleculeCameraTest::stereo_eye_separation_tracks_camera_distance() {
  QCOMPARE(molecule_camera::stereo_eye_separation(-1.0f), 0.0f);
  QCOMPARE(molecule_camera::stereo_eye_separation(9.0f),
           9.0f / molecule_camera::kStereoSeparationDivisor);
}

void MoleculeCameraTest::default_view_uses_negative_x_with_positive_z_up() {
  const molecule_camera::ViewFrame mono = molecule_camera::default_view(8.0f);
  QCOMPARE(mono.eye, QVector3D(8.0f, 0.0f, 0.0f));
  QCOMPARE(mono.center, QVector3D(0.0f, 0.0f, 0.0f));
  QCOMPARE(mono.up, QVector3D(0.0f, 0.0f, 1.0f));
  QCOMPARE((mono.center - mono.eye).normalized(), QVector3D(-1.0f, 0.0f, 0.0f));

  const molecule_camera::ViewFrame stereo_eye = molecule_camera::default_view(8.0f, 0.2f);
  QCOMPARE((stereo_eye.center - stereo_eye.eye).normalized(), QVector3D(-1.0f, 0.0f, 0.0f));
  QCOMPARE(molecule_camera::camera_space_direction(QVector3D(0.0f, 1.0f, 0.0f)),
           QVector3D(1.0f, 0.0f, 0.0f));
  QCOMPARE(molecule_camera::camera_space_direction(QVector3D(0.0f, 0.0f, 1.0f)),
           QVector3D(0.0f, 1.0f, 0.0f));
}

QTEST_APPLESS_MAIN(MoleculeCameraTest)

#include "molecule_camera_test.moc"
