#include "proj4d/mouse_input.hpp"

namespace proj4d {

MouseMotionMode selectMouseMotionMode(bool controlHeld, bool shiftHeld) {
  if (controlHeld) {
    return MouseMotionMode::VisionCubeOrbit;
  }
  if (shiftHeld) {
    return MouseMotionMode::VerticalLook;
  }
  return MouseMotionMode::FourthDimensionalLook;
}

MouseMotionMapping mapMouseMotion(double horizontalPixels,
                                  double verticalPixels, MouseMotionMode mode) {
  const double horizontalTurn = horizontalPixels * mouseLookRadiansPerPixel;
  const double verticalTurn = verticalPixels * mouseLookRadiansPerPixel;
  if (mode == MouseMotionMode::VisionCubeOrbit) {
    return {0.0, 0.0, 0.0, horizontalTurn, verticalTurn};
  }
  if (mode == MouseMotionMode::VerticalLook) {
    return {horizontalTurn, -verticalTurn, 0.0, 0.0, 0.0};
  }
  return {horizontalTurn, 0.0, verticalTurn, 0.0, 0.0};
}

} // namespace proj4d
