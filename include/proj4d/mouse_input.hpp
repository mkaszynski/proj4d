#pragma once

namespace proj4d {

inline constexpr double mouseLookRadiansPerPixel = 0.004;

enum class MouseMotionMode {
  FourthDimensionalLook,
  VerticalLook,
  VisionCubeOrbit,
};

struct MouseMotionMapping {
  double worldHorizontalTurn{};
  double worldVerticalTurn{};
  double worldFourthTurn{};
  double visionCubeYawTurn{};
  double visionCubePitchTurn{};
};

[[nodiscard]] MouseMotionMode selectMouseMotionMode(bool controlHeld,
                                                    bool shiftHeld);
[[nodiscard]] MouseMotionMapping mapMouseMotion(double horizontalPixels,
                                                double verticalPixels,
                                                MouseMotionMode mode);

} // namespace proj4d
