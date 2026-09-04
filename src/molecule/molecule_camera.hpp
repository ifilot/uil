#pragma once

#include <QVector3D>

namespace molecule_camera {

/** @brief World-space vectors defining a camera view. */
struct ViewFrame {
  QVector3D eye;
  QVector3D center;
  QVector3D up;
};

constexpr float kVerticalFieldOfViewDegrees = 35.0f;
constexpr float kMinimumDistanceFactor = 0.30f;
constexpr float kMaximumDistanceFactor = 3.0f;
constexpr float kStereoSeparationDivisor = 30.0f;

/** @brief Returns a camera distance that fits a centered bounding sphere. */
float fit_distance(float bounding_radius, float viewport_aspect);

/** @brief Applies zoom and prevents the camera from entering the molecule. */
float distance(float bounding_radius, float viewport_aspect, float distance_factor);

/** @brief Returns a conservative eye separation for a convergent stereo pair. */
float stereo_eye_separation(float camera_distance);

/** @brief Builds the default view looking along -X with +Z pointing upward. */
ViewFrame default_view(float camera_distance, float horizontal_eye_offset = 0.0f);

/** @brief Maps a world-space direction to screen-right, screen-up, and camera-depth axes. */
QVector3D camera_space_direction(const QVector3D& world_direction);

/** @brief Updates the camera-distance multiplier for a wheel or trackpad delta. */
float zoomed_distance_factor(float current_factor, int angle_delta_y, int pixel_delta_y);

/** @brief Projects a world-space radius at a camera-space depth into logical pixels. */
float projected_radius(float world_radius, float rotated_z, float camera_distance,
                       float viewport_height);

}  // namespace molecule_camera
