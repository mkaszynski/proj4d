#include "proj4d/player_motion.hpp"

#include <array>
#include <cmath>

namespace proj4d {

namespace {

constexpr double movementSpeed = 2.4;
constexpr double gravity = -8.5;

} // namespace

BlockCoord playerLowerBodyBlock(const Vec4 &eyePosition) {
  return containingBlock({
      eyePosition.x,
      eyePosition.y - playerEyeHeight + playerCollisionInset,
      eyePosition.z,
      eyePosition.w,
  });
}

bool playerCanOccupy(const BlockWorld &world, const Vec4 &eyePosition) {
  return !world.isSolid(containingBlock(eyePosition)) &&
         !world.isSolid(playerLowerBodyBlock(eyePosition));
}

void requestPlayerJump(PlayerMotionState &state) {
  if (!state.grounded) {
    return;
  }
  state.verticalVelocity = std::sqrt(-2.0 * gravity * playerJumpHeight);
  state.grounded = false;
}

void updatePlayerMotion(Camera4D &camera, const BlockWorld &world,
                        PlayerMotionState &state, double moveInput,
                        double deltaSeconds) {
  if (moveInput != 0.0) {
    const Vec4 movement =
        camera.flattenedForward() * (moveInput * movementSpeed * deltaSeconds);
    constexpr std::array<std::size_t, 3> horizontalAxes{0U, 2U, 3U};
    for (const std::size_t axis : horizontalAxes) {
      Vec4 candidate = camera.position;
      candidate[axis] += movement[axis];
      if (playerCanOccupy(world, candidate)) {
        camera.position = candidate;
      }
    }
  }

  camera.position.y += state.verticalVelocity * deltaSeconds +
                       0.5 * gravity * deltaSeconds * deltaSeconds;
  state.verticalVelocity += gravity * deltaSeconds;
  const BlockCoord below = containingBlock({
      camera.position.x,
      camera.position.y - playerEyeHeight - playerCollisionInset,
      camera.position.z,
      camera.position.w,
  });
  if (state.verticalVelocity <= 0.0 && world.isSolid(below)) {
    camera.position.y = static_cast<double>(below.y + 1) + playerEyeHeight;
    state.verticalVelocity = 0.0;
    state.grounded = true;
  } else {
    state.grounded = false;
  }

  if (camera.position.y < -4096.0) {
    camera.position = state.spawnPosition;
    state.verticalVelocity = 0.0;
    state.grounded = false;
  }
}

} // namespace proj4d
