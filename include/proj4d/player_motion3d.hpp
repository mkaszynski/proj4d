#pragma once

#include "proj4d/camera3d.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/world3d.hpp"

namespace proj4d {

struct PlayerMotionState3D {
  double verticalVelocity{};
  bool grounded{};
  Vec3 spawnPosition{};
  bool sneaking{};
};

struct PlayerMoveInput3D {
  double forward{};
  double strafe{};
  bool sneak{};
};

[[nodiscard]] BlockCoord3D
playerLowerBodyBlock(const Vec3 &eyePosition,
                     PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] bool
playerCollidesAt(const BlockWorld3D &world, const Vec3 &eyePosition,
                 PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] Vec3
resolvePlayerMotion(const BlockWorld3D &world, const Vec3 &eyePosition,
                    const Vec3 &desiredMotion,
                    PlayerCollisionBounds bounds = playerCollisionBounds);
void updatePlayerMotion(Camera3D &camera, const BlockWorld3D &world,
                        PlayerMotionState3D &state, PlayerMoveInput3D moveInput,
                        bool jumpInput, double deltaSeconds);

} // namespace proj4d
