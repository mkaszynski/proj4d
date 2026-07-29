#pragma once

#include <optional>

#include "proj4d/math.hpp"

namespace proj4d {

struct ProjectedPoint {
  Vec3 position{};
  double depth{};
};

class Camera4D {
public:
  Vec4 position{0.5, 2.1, -3.5, 0.5};
  Vec4 imageX{1.0, 0.0, 0.0, 0.0};
  Vec4 imageY{0.0, 1.0, 0.0, 0.0};
  Vec4 imageZ{0.0, 0.0, 0.0, 1.0};
  Vec4 forward{0.0, 0.0, 1.0, 0.0};
  double focalLength{0.72};
  double nearPlane{0.08};
  double farPlane{24.0};

  void turnHorizontal(double radians);
  void turnVertical(double radians);
  void turnFourth(double radians);
  [[nodiscard]] std::optional<ProjectedPoint> project(const Vec4 &point) const;
  [[nodiscard]] Vec4 flattenedForward() const;

private:
  void rotateViewPlane(Vec4 &imageAxis, double radians);
};

} // namespace proj4d
