#include "proj4d/camera.hpp"

#include <algorithm>
#include <cmath>

namespace proj4d {

void Camera4D::rotateHorizontalPlane(Vec4 &imageAxis, double radians) {
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const Vec4 oldForward = horizontalForward_;
  const Vec4 oldAxis = imageAxis;
  horizontalForward_ = oldForward * cosine + oldAxis * sine;
  imageAxis = oldAxis * cosine - oldForward * sine;
  rebuildPitchedFrame();
}

void Camera4D::rebuildPitchedFrame() {
  constexpr Vec4 worldUp{0.0, 1.0, 0.0, 0.0};
  const double cosine = std::cos(verticalPitch_);
  const double sine = std::sin(verticalPitch_);
  forward = horizontalForward_ * cosine + worldUp * sine;
  imageY = worldUp * cosine - horizontalForward_ * sine;
}

void Camera4D::turnHorizontal(double radians) {
  rotateHorizontalPlane(imageX, radians);
}

void Camera4D::turnVertical(double radians) {
  verticalPitch_ = std::clamp(verticalPitch_ + radians, -straightVerticalPitch,
                              straightVerticalPitch);
  rebuildPitchedFrame();
}

void Camera4D::turnFourth(double radians) {
  rotateHorizontalPlane(imageZ, radians);
}

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

Vec4 Camera4D::flattenedForward() const { return horizontalForward_; }

double Camera4D::verticalPitch() const { return verticalPitch_; }

} // namespace proj4d
