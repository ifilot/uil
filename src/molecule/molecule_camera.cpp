#include "molecule/molecule_camera.hpp"

#include <algorithm>
#include <cmath>

namespace molecule_camera {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kFramingMargin = 1.08f;
constexpr float kMinimumCameraClearance = 1.05f;
constexpr float kWheelZoomPerStep = 0.82f;

float radians(float degrees) { return degrees * kPi / 180.0f; }
}  // namespace

float fit_distance(float bounding_radius, float viewport_aspect) {
  const float radius = std::max(bounding_radius, 0.001f);
  const float aspect = std::max(viewport_aspect, 0.001f);
  const float vertical_half_fov = radians(kVerticalFieldOfViewDegrees * 0.5f);
  const float horizontal_half_fov = std::atan(std::tan(vertical_half_fov) * aspect);
  const float limiting_half_fov = std::min(vertical_half_fov, horizontal_half_fov);
  return kFramingMargin * radius / std::sin(limiting_half_fov);
}

float distance(float bounding_radius, float viewport_aspect, float distance_factor) {
  const float radius = std::max(bounding_radius, 0.001f);
  const float factor = std::clamp(distance_factor, kMinimumDistanceFactor, kMaximumDistanceFactor);
  return std::max(fit_distance(radius, viewport_aspect) * factor, radius * kMinimumCameraClearance);
}

float stereo_eye_separation(float camera_distance) {
  return std::max(camera_distance, 0.0f) / kStereoSeparationDivisor;
}

float zoomed_distance_factor(float current_factor, int angle_delta_y, int pixel_delta_y) {
  const float delta_steps =
      angle_delta_y != 0 ? float(angle_delta_y) / 120.0f : float(pixel_delta_y) / 80.0f;
  return std::clamp(current_factor * std::pow(kWheelZoomPerStep, delta_steps),
                    kMinimumDistanceFactor, kMaximumDistanceFactor);
}

float projected_radius(float world_radius, float rotated_z, float camera_distance,
                       float viewport_height) {
  const float depth = camera_distance - rotated_z;
  if (world_radius <= 0.0f || depth <= 0.0f || viewport_height <= 0.0f) {
    return 0.0f;
  }
  const float focal_length =
      viewport_height / (2.0f * std::tan(radians(kVerticalFieldOfViewDegrees * 0.5f)));
  return focal_length * world_radius / depth;
}

}  // namespace molecule_camera
