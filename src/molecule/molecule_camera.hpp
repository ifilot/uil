#pragma once

namespace molecule_camera {

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

/** @brief Updates the camera-distance multiplier for a wheel or trackpad delta. */
float zoomed_distance_factor(float current_factor, int angle_delta_y, int pixel_delta_y);

/** @brief Projects a world-space radius at a camera-space depth into logical pixels. */
float projected_radius(float world_radius, float rotated_z, float camera_distance,
                       float viewport_height);

}  // namespace molecule_camera
