#pragma once

#include <optional>

#include "proj4d/camera.hpp"
#include "proj4d/math.hpp"

namespace proj4d {

struct ProjectedPoint2D {
  Vec2 position{};
  double depth{};
};

class Camera3D {
public:
  Vec3 position{0.5, 2.65, 0.5};
  double focalLength{1.2};
  double nearPlane{0.08};
  double farPlane{24.0};

  void turnHorizontal(double radians);
  void turnVertical(double radians);
  [[nodiscard]] Vec3 forward() const;
  [[nodiscard]] Vec3 right() const;
  [[nodiscard]] Vec3 up() const;
  [[nodiscard]] Vec3 movementForward() const;
  [[nodiscard]] Vec3 rayDirectionAt(double imageX, double imageY,
                                    double aspectRatio) const;
  [[nodiscard]] std::optional<ProjectedPoint2D>
  project(const Vec3 &point, double aspectRatio) const;
  [[nodiscard]] double horizontalYaw() const;
  [[nodiscard]] double verticalPitch() const;

private:
  double horizontalYaw_{};
  double verticalPitch_{};
};

} // namespace proj4d
