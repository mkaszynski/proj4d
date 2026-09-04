#pragma once

#include <optional>

#include "proj4d/camera.hpp"
#include "proj4d/math.hpp"

namespace proj4d {

struct ProjectedPoint1D {
  double position{};
  double depth{};
};

class Camera2D {
public:
  Vec2 position{0.5, 2.65};
  double focalLength{0.72};
  double nearPlane{0.08};
  double farPlane{24.0};

  void turnVertical(double radians);
  void reverseHorizontalView();
  [[nodiscard]] Vec2 forward() const;
  [[nodiscard]] Vec2 imageAxis() const;
  [[nodiscard]] Vec2 movementForward() const;
  [[nodiscard]] Vec2 rayDirectionAt(double imagePosition) const;
  [[nodiscard]] std::optional<ProjectedPoint1D>
  project(const Vec2 &point) const;
  [[nodiscard]] double verticalPitch() const;
  [[nodiscard]] int horizontalDirection() const;

private:
  double verticalPitch_{};
  int horizontalDirection_{1};
};

} // namespace proj4d
