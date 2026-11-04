#include "proj4d/mouse_input.hpp"

namespace proj4d {

MouseMotionMode selectMouseMotionMode(bool controlHeld, bool verticalLookHeld) {
  if (controlHeld) {
    return MouseMotionMode::VisionCubeOrbit;
  }
  if (verticalLookHeld) {
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
    return {0.0, -verticalTurn, 0.0, 0.0, 0.0};
  }
  return {horizontalTurn, 0.0, verticalTurn, 0.0, 0.0};
}

void beginInventoryRightGesture(InventoryRightGesture &gesture, int x, int y) {
  gesture = {x, y, true, false};
}

bool updateInventoryRightGesture(InventoryRightGesture &gesture, int x, int y) {
  if (!gesture.pressed) {
    return false;
  }
  const long long horizontal = static_cast<long long>(x) - gesture.pressX;
  const long long vertical = static_cast<long long>(y) - gesture.pressY;
  constexpr long long thresholdSquared =
      inventoryRightDragThresholdPixels * inventoryRightDragThresholdPixels;
  if (horizontal * horizontal + vertical * vertical >= thresholdSquared) {
    gesture.dragging = true;
  }
  return gesture.dragging;
}

bool finishInventoryRightGesture(InventoryRightGesture &gesture) {
  const bool clicked = gesture.pressed && !gesture.dragging;
  gesture = {};
  return clicked;
}

void cancelInventoryRightGesture(InventoryRightGesture &gesture) {
  gesture = {};
}

} // namespace proj4d
