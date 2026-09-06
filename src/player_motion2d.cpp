#include "proj4d/player_motion2d.hpp"

#include <algorithm>
#include <cmath>

namespace proj4d {

namespace {

int blockMinimum(double value) {
  return static_cast<int>(std::floor(value + playerCollisionEpsilon));
}

int blockMaximum(double value) {
  return static_cast<int>(std::floor(value - playerCollisionEpsilon));
}

bool hasGroundContact(const BlockWorld2D &world, const Vec2 &eyePosition,
                      PlayerCollisionBounds bounds) {
  Vec2 probe = eyePosition;
  probe.y -= playerGroundProbeDistance;
  return playerCollidesAt(world, probe, bounds);
}

Vec2 resolveStep(const BlockWorld2D &world, const Vec2 &eyePosition,
                 const Vec2 &desiredMotion, PlayerCollisionBounds bounds,
                 bool protectGroundEdge) {
  Vec2 accepted{};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    Vec2 axisMotion{};
    axisMotion[axis] = desiredMotion[axis];
    const Vec2 candidate = eyePosition + accepted + axisMotion;
    if (playerCollidesAt(world, candidate, bounds)) {
      continue;
    }
    if (protectGroundEdge && axis == 0U &&
        !hasGroundContact(world, candidate, bounds)) {
      continue;
    }
    accepted[axis] = axisMotion[axis];
  }
  return accepted;
}

Vec2 resolveWithEdgeProtection(const BlockWorld2D &world,
                               const Vec2 &eyePosition,
                               const Vec2 &desiredMotion,
                               PlayerCollisionBounds bounds,
                               bool protectGroundEdge) {
  const double maximumMotion =
      std::max(std::abs(desiredMotion.x), std::abs(desiredMotion.y));
  const int steps = std::max(
      1,
      static_cast<int>(std::ceil(maximumMotion / playerMaximumStepDistance)));
  const Vec2 stepMotion = desiredMotion * (1.0 / static_cast<double>(steps));
  Vec2 acceptedTotal{};
  Vec2 currentEye = eyePosition;
  for (int step = 0; step < steps; ++step) {
    const Vec2 accepted =
        resolveStep(world, currentEye, stepMotion, bounds, protectGroundEdge);
    acceptedTotal += accepted;
    currentEye += accepted;
  }
  return acceptedTotal;
}

void updateSneakPose(Camera2D &camera, const BlockWorld2D &world,
                     PlayerMotionState2D &state, bool sneakInput) {
  if (sneakInput && !state.sneaking) {
    camera.position.y -= playerSneakEyeDrop;
    state.sneaking = true;
    return;
  }
  if (!sneakInput && state.sneaking) {
    Vec2 standingEye = camera.position;
    standingEye.y += playerSneakEyeDrop;
    if (!playerCollidesAt(world, standingEye, playerCollisionBounds)) {
      camera.position = standingEye;
      state.sneaking = false;
    }
  }
}

} // namespace

BlockCoord2D playerLowerBodyBlock(const Vec2 &eyePosition,
                                  PlayerCollisionBounds bounds) {
  const Vec2 lowerBodyPoint{eyePosition.x, eyePosition.y - bounds.eyeToFeet +
                                               playerCollisionEpsilon};
  return containingBlock(lowerBodyPoint);
}

bool playerCollidesAt(const BlockWorld2D &world, const Vec2 &eyePosition,
                      PlayerCollisionBounds bounds) {
  const int minimumX = blockMinimum(eyePosition.x - bounds.radius);
  const int minimumY = blockMinimum(eyePosition.y - bounds.eyeToFeet);
  const int maximumX = blockMaximum(eyePosition.x + bounds.radius);
  const int maximumY = blockMaximum(eyePosition.y + bounds.eyeToHead);
  for (int y = minimumY; y <= maximumY; ++y) {
    for (int x = minimumX; x <= maximumX; ++x) {
      if (world.isSolid({x, y})) {
        return true;
      }
    }
  }
  return false;
}

Vec2 resolvePlayerMotion(const BlockWorld2D &world, const Vec2 &eyePosition,
                         const Vec2 &desiredMotion,
                         PlayerCollisionBounds bounds) {
  return resolveWithEdgeProtection(world, eyePosition, desiredMotion, bounds,
                                   false);
}

void updatePlayerMotion(Camera2D &camera, const BlockWorld2D &world,
                        PlayerMotionState2D &state, PlayerMoveInput2D moveInput,
                        bool jumpInput, double deltaSeconds) {
  deltaSeconds = std::clamp(deltaSeconds, 0.0, playerMaximumDeltaSeconds);
  updateSneakPose(camera, world, state, moveInput.sneak);
  const PlayerCollisionBounds activeBounds =
      state.sneaking ? playerSneakCollisionBounds : playerCollisionBounds;
  const double movementSpeed =
      playerWalkSpeed * (state.sneaking ? playerSneakSpeedMultiplier : 1.0);
  const Vec2 planarMotion = camera.movementForward() *
                            (moveInput.forward * movementSpeed * deltaSeconds);
  const double maximumMotion = std::max(
      std::abs(planarMotion.x),
      std::abs((state.verticalVelocity - playerGravity * deltaSeconds) *
               deltaSeconds));
  const int steps = std::max(
      1,
      static_cast<int>(std::ceil(maximumMotion / playerMaximumStepDistance)));

  for (int step = 0; step < steps; ++step) {
    const double stepDelta = deltaSeconds / static_cast<double>(steps);
    const bool jumpRequested = jumpInput && step == 0;
    state.grounded = hasGroundContact(world, camera.position, activeBounds);
    if (state.grounded && state.verticalVelocity < 0.0) {
      state.verticalVelocity = 0.0;
    }
    if (jumpRequested && state.grounded) {
      state.verticalVelocity =
          std::sqrt(2.0 * playerGravity * playerJumpHeight);
      state.grounded = false;
    }

    state.verticalVelocity -= playerGravity * stepDelta;
    const Vec2 desiredMotion{planarMotion.x / static_cast<double>(steps),
                             state.verticalVelocity * stepDelta};
    const Vec2 acceptedMotion = resolveWithEdgeProtection(
        world, camera.position, desiredMotion, activeBounds,
        state.sneaking && state.grounded);
    camera.position += acceptedMotion;

    if (desiredMotion.y < 0.0 && acceptedMotion.y == 0.0) {
      state.verticalVelocity = 0.0;
      state.grounded = true;
    } else if (desiredMotion.y > 0.0 && acceptedMotion.y == 0.0) {
      state.verticalVelocity = 0.0;
      state.grounded = false;
    } else {
      state.grounded = hasGroundContact(world, camera.position, activeBounds);
    }
  }

  if (camera.position.y < -4096.0) {
    camera.position = state.spawnPosition;
    state.verticalVelocity = 0.0;
    state.grounded = false;
    state.sneaking = false;
  }
}

} // namespace proj4d
