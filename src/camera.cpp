#include "proj4d/camera.hpp"

#include <cmath>

namespace proj4d {

void Camera4D::rotateViewPlane(Vec4 &imageAxis, double radians) {
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const Vec4 oldForward = forward;
  const Vec4 oldAxis = imageAxis;
  forward = oldForward * cosine + oldAxis * sine;
  imageAxis = oldAxis * cosine - oldForward * sine;
}

void Camera4D::turnHorizontal(double radians) {
  rotateViewPlane(imageX, radians);
}

void Camera4D::turnVertical(double radians) {
  rotateViewPlane(imageY, radians);
}

void Camera4D::turnFourth(double radians) { rotateViewPlane(imageZ, radians); }

std::optional<ProjectedPoint> Camera4D::project(const Vec4 &point) const {
  const Vec4 relative = point - position;
  const double depth = dot(relative, forward);
  if (depth < nearPlane || depth > farPlane) {
    return std::nullopt;
  }
  const double scale = focalLength / depth;
  return ProjectedPoint{{
                            dot(relative, imageX) * scale,
                            dot(relative, imageY) * scale,
                            dot(relative, imageZ) * scale,
                        },
                        depth};
}

Vec4 Camera4D::flattenedForward() const {
  Vec4 horizontal = forward;
  horizontal.y = 0.0;
  const double magnitude = length(horizontal);
  return magnitude <= 1.0e-9 ? Vec4{0.0, 0.0, 1.0, 0.0}
                             : horizontal * (1.0 / magnitude);
}

} // namespace proj4d
