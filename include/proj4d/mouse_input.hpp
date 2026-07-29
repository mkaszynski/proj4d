#pragma once

namespace proj4d {

inline constexpr double mouseLookRadiansPerPixel = 0.004;

struct MouseMotionMapping {
  double worldHorizontalTurn{};
  double worldFourthTurn{};
  double visionCubeYawTurn{};
  double visionCubePitchTurn{};
};

[[nodiscard]] MouseMotionMapping mapMouseMotion(double horizontalPixels,
                                                double verticalPixels,
                                                bool orbitVisionCube);

} // namespace proj4d
