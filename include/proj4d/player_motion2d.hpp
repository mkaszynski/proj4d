#pragma once

#include "proj4d/camera2d.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/world2d.hpp"

namespace proj4d {

struct PlayerMotionState2D {
  double verticalVelocity{};
  bool grounded{};
  Vec2 spawnPosition{};
  bool sneaking{};
};

struct PlayerMoveInput2D {
  double forward{};
  bool sneak{};
};

[[nodiscard]] BlockCoord2D
playerLowerBodyBlock(const Vec2 &eyePosition,
                     PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] bool
playerCollidesAt(const BlockWorld2D &world, const Vec2 &eyePosition,
                 PlayerCollisionBounds bounds = playerCollisionBounds);
[[nodiscard]] Vec2
resolvePlayerMotion(const BlockWorld2D &world, const Vec2 &eyePosition,
                    const Vec2 &desiredMotion,
                    PlayerCollisionBounds bounds = playerCollisionBounds);
void updatePlayerMotion(Camera2D &camera, const BlockWorld2D &world,
                        PlayerMotionState2D &state, PlayerMoveInput2D moveInput,
                        bool jumpInput, double deltaSeconds);

} // namespace proj4d
