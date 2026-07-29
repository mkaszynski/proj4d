#pragma once

#include "proj4d/camera.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

inline constexpr double playerEyeHeight = 1.1;
inline constexpr double playerCollisionInset = 0.02;
inline constexpr double playerJumpHeight = 1.5;

struct PlayerMotionState {
  double verticalVelocity{};
  bool grounded{};
  Vec4 spawnPosition{};
};

[[nodiscard]] BlockCoord playerLowerBodyBlock(const Vec4 &eyePosition);
[[nodiscard]] bool playerCanOccupy(const BlockWorld &world,
                                   const Vec4 &eyePosition);
void requestPlayerJump(PlayerMotionState &state);
void updatePlayerMotion(Camera4D &camera, const BlockWorld &world,
                        PlayerMotionState &state, double moveInput,
                        double deltaSeconds);

} // namespace proj4d
