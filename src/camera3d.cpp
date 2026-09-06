#include "proj4d/camera3d.hpp"

#include <algorithm>
#include <cmath>

namespace proj4d {

void Camera3D::turnHorizontal(double radians) { horizontalYaw_ += radians; }

void Camera3D::turnVertical(double radians) {
  verticalPitch_ = std::clamp(verticalPitch_ + radians, -straightVerticalPitch,
                              straightVerticalPitch);
}

Vec3 Camera3D::forward() const {
  const double horizontal = std::cos(verticalPitch_);
  return {horizontal * std::cos(horizontalYaw_), std::sin(verticalPitch_),
          horizontal * std::sin(horizontalYaw_)};
}

Vec3 Camera3D::right() const {
  return {-std::sin(horizontalYaw_), 0.0, std::cos(horizontalYaw_)};
}

Vec3 Camera3D::up() const {
  return {-std::sin(verticalPitch_) * std::cos(horizontalYaw_),
          std::cos(verticalPitch_),
          -std::sin(verticalPitch_) * std::sin(horizontalYaw_)};
}

Vec3 Camera3D::movementForward() const {
  return {std::cos(horizontalYaw_), 0.0, std::sin(horizontalYaw_)};
}

Vec3 Camera3D::rayDirectionAt(double imageX, double imageY,
                              double aspectRatio) const {
  return normalized(forward() * focalLength + right() * (imageX * aspectRatio) +
                    up() * imageY);
}

std::optional<ProjectedPoint2D> Camera3D::project(const Vec3 &point,
                                                  double aspectRatio) const {
  const Vec3 relative = point - position;
  const double depth = dot(relative, forward());
  if (depth < nearPlane || depth > farPlane || aspectRatio <= 0.0) {
    return std::nullopt;
  }
  return ProjectedPoint2D{
      {dot(relative, right()) * focalLength / (depth * aspectRatio),
       dot(relative, up()) * focalLength / depth},
      depth};
}

double Camera3D::horizontalYaw() const { return horizontalYaw_; }

double Camera3D::verticalPitch() const { return verticalPitch_; }

} // namespace proj4d
