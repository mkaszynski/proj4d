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
inline constexpr double playerSneakSpeedMultiplier = 0.3;
inline constexpr double playerSneakEyeDrop = 0.3;
inline constexpr double playerCollisionEpsilon = 0.0001;
inline constexpr double playerGroundProbeDistance = 0.04;
inline constexpr double playerMaximumStepDistance = 0.25;
inline constexpr double playerMaximumDeltaSeconds = 0.05;
inline constexpr double playerSwimSpeed = 3.5;
inline constexpr double playerSwimVerticalSpeed = 4.0;
inline constexpr double playerFluidGravity = 6.0;
inline constexpr double playerFluidDrag = 4.0;
inline constexpr PlayerCollisionBounds playerSneakCollisionBounds{
    playerCollisionBounds.radius,
    playerCollisionBounds.eyeToFeet - playerSneakEyeDrop,
    playerCollisionBounds.eyeToHead,
};

struct PlayerMotionState {
  double verticalVelocity{};
  bool grounded{};
  Vec4 spawnPosition{};
  bool sneaking{};
  bool swimming{};
};

struct PlayerMoveInput {
  double forward{};
  double ordinaryStrafe{};
  double fourthStrafe{};
  bool sneak{};
};

[[nodiscard]] BlockCoord
playerLowerBodyBlock(const Vec4 &eyePosition,
                     PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] bool
playerCollidesAt(const BlockWorld &world, const Vec4 &eyePosition,
                 PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] bool playerCanOccupy(const BlockWorld &world,
                                   const Vec4 &eyePosition);
[[nodiscard]] bool
playerIntersectsFluid(const BlockWorld &world, const Vec4 &eyePosition,
                      PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] Vec4
resolvePlayerMotion(const BlockWorld &world, const Vec4 &eyePosition,
                    const Vec4 &desiredMotion,
                    PlayerCollisionBounds bounds = playerCollisionBounds);
void updatePlayerMotion(Camera4D &camera, const BlockWorld &world,
                        PlayerMotionState &state, PlayerMoveInput moveInput,
                        bool jumpInput, double deltaSeconds);

} // namespace proj4d
