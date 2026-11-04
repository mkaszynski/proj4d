#pragma once

namespace proj4d {

inline constexpr double mouseLookRadiansPerPixel = 0.004;
inline constexpr int inventoryRightDragThresholdPixels = 5;

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

struct InventoryRightGesture {
  int pressX{};
  int pressY{};
  bool pressed{};
  bool dragging{};
};

[[nodiscard]] MouseMotionMode selectMouseMotionMode(bool controlHeld,
                                                    bool verticalLookHeld);
[[nodiscard]] MouseMotionMapping mapMouseMotion(double horizontalPixels,
                                                double verticalPixels,
                                                MouseMotionMode mode);
void beginInventoryRightGesture(InventoryRightGesture &gesture, int x, int y);
[[nodiscard]] bool updateInventoryRightGesture(InventoryRightGesture &gesture,
                                               int x, int y);
[[nodiscard]] bool finishInventoryRightGesture(InventoryRightGesture &gesture);
void cancelInventoryRightGesture(InventoryRightGesture &gesture);

} // namespace proj4d
