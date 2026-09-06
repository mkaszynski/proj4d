#include "proj4d/camera2d.hpp"

#include <algorithm>
#include <cmath>

namespace proj4d {

void Camera2D::turnVertical(double radians) {
  verticalPitch_ = std::clamp(verticalPitch_ + radians, -straightVerticalPitch,
                              straightVerticalPitch);
}

void Camera2D::reverseHorizontalView() {
  horizontalDirection_ = -horizontalDirection_;
}

Vec2 Camera2D::forward() const {
  return {static_cast<double>(horizontalDirection_) * std::cos(verticalPitch_),
          std::sin(verticalPitch_)};
}

Vec2 Camera2D::imageAxis() const {
  return {-static_cast<double>(horizontalDirection_) * std::sin(verticalPitch_),
          std::cos(verticalPitch_)};
}

Vec2 Camera2D::movementForward() const {
  return {static_cast<double>(horizontalDirection_), 0.0};
}

Vec2 Camera2D::rayDirectionAt(double imagePosition) const {
  return normalized(forward() * focalLength + imageAxis() * imagePosition);
}

std::optional<ProjectedPoint1D> Camera2D::project(const Vec2 &point) const {
  const Vec2 relative = point - position;
  const double depth = dot(relative, forward());
  if (depth < nearPlane || depth > farPlane) {
    return std::nullopt;
  }
  return ProjectedPoint1D{dot(relative, imageAxis()) * focalLength / depth,
                          depth};
}

double Camera2D::verticalPitch() const { return verticalPitch_; }

int Camera2D::horizontalDirection() const { return horizontalDirection_; }

} // namespace proj4d
