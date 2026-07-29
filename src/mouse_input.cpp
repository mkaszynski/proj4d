#include "proj4d/mouse_input.hpp"

namespace proj4d {

MouseMotionMapping mapMouseMotion(double horizontalPixels,
                                  double verticalPixels, bool orbitVisionCube) {
  const double horizontalTurn = horizontalPixels * mouseLookRadiansPerPixel;
  const double verticalTurn = verticalPixels * mouseLookRadiansPerPixel;
  if (orbitVisionCube) {
    return {0.0, 0.0, horizontalTurn, verticalTurn};
  }
  return {horizontalTurn, verticalTurn, 0.0, 0.0};
}

} // namespace proj4d
