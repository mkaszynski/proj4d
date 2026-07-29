#include "proj4d/camera.hpp"

#include <algorithm>
#include <cmath>

namespace proj4d {

namespace {

double wrappedAngle(double radians) {
  return std::remainder(radians, straightVerticalPitch * 4.0);
}

} // namespace

void Camera4D::rebuildFrame() {
  constexpr Vec4 worldUp{0.0, 1.0, 0.0, 0.0};
  const double horizontalCosine = std::cos(horizontalAngle_);
  const double horizontalSine = std::sin(horizontalAngle_);
  const double fourthCosine = std::cos(fourthAngle_);
  const double fourthSine = std::sin(fourthAngle_);
  const double verticalCosine = std::cos(verticalPitch_);
  const double verticalSine = std::sin(verticalPitch_);

  horizontalForward_ = {
      fourthCosine * horizontalSine,
      0.0,
      fourthCosine * horizontalCosine,
      fourthSine,
  };
  imageX = {horizontalCosine, 0.0, -horizontalSine, 0.0};
  imageZ = {
      -fourthSine * horizontalSine,
      0.0,
      -fourthSine * horizontalCosine,
      fourthCosine,
  };
  forward = horizontalForward_ * verticalCosine + worldUp * verticalSine;
  imageY = worldUp * verticalCosine - horizontalForward_ * verticalSine;
}

void Camera4D::turnHorizontal(double radians) {
  if (radians == 0.0) {
    return;
  }
  horizontalAngle_ = wrappedAngle(horizontalAngle_ + radians);
  rebuildFrame();
}

void Camera4D::turnVertical(double radians) {
  if (radians == 0.0) {
    return;
  }
  verticalPitch_ = std::clamp(verticalPitch_ + radians, -straightVerticalPitch,
                              straightVerticalPitch);
  rebuildFrame();
}

void Camera4D::turnFourth(double radians) {
  if (radians == 0.0) {
    return;
  }
  fourthAngle_ = wrappedAngle(fourthAngle_ + radians);
  rebuildFrame();
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

double Camera4D::horizontalAngle() const { return horizontalAngle_; }

double Camera4D::fourthAngle() const { return fourthAngle_; }

} // namespace proj4d
