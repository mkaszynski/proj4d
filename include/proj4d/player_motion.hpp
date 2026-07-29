#pragma once

#include "proj4d/camera.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

struct PlayerCollisionBounds {
  double radius{0.15};
  double eyeToFeet{1.65};
  double eyeToHead{0.18};
};

inline constexpr PlayerCollisionBounds playerCollisionBounds{};
inline constexpr double playerEyeHeight = playerCollisionBounds.eyeToFeet;
inline constexpr double playerWalkSpeed = 7.0;
inline constexpr double playerGravity = 36.0;
inline constexpr double playerJumpHeight = 1.5;

struct PlayerMotionState {
  double verticalVelocity{};
  bool grounded{};
  Vec4 spawnPosition{};
};

struct PlayerMoveInput {
  double forward{};
  double ordinaryStrafe{};
  double fourthStrafe{};
};

[[nodiscard]] BlockCoord playerLowerBodyBlock(const Vec4 &eyePosition);
[[nodiscard]] bool
playerCollidesAt(const BlockWorld &world, const Vec4 &eyePosition,
                 PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] bool playerCanOccupy(const BlockWorld &world,
                                   const Vec4 &eyePosition);
[[nodiscard]] Vec4
resolvePlayerMotion(const BlockWorld &world, const Vec4 &eyePosition,
                    const Vec4 &desiredMotion,
                    PlayerCollisionBounds bounds = playerCollisionBounds);
void updatePlayerMotion(Camera4D &camera, const BlockWorld &world,
                        PlayerMotionState &state, PlayerMoveInput moveInput,
                        bool jumpInput, double deltaSeconds);

} // namespace proj4d
