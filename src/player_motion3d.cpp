#include "proj4d/player_motion3d.hpp"

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

bool hasGroundContact(const BlockWorld3D &world, const Vec3 &eyePosition,
                      PlayerCollisionBounds bounds) {
  Vec3 probe = eyePosition;
  probe.y -= playerGroundProbeDistance;
  return playerCollidesAt(world, probe, bounds);
}

Vec3 resolveStep(const BlockWorld3D &world, const Vec3 &eyePosition,
                 const Vec3 &desiredMotion, PlayerCollisionBounds bounds,
                 bool protectGroundEdge) {
  Vec3 accepted{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    Vec3 axisMotion{};
    axisMotion[axis] = desiredMotion[axis];
    const Vec3 candidate = eyePosition + accepted + axisMotion;
    if (playerCollidesAt(world, candidate, bounds)) {
      continue;
    }
    if (protectGroundEdge && axis != 1U &&
        !hasGroundContact(world, candidate, bounds)) {
      continue;
    }
    accepted[axis] = axisMotion[axis];
  }
  return accepted;
}

Vec3 resolveWithEdgeProtection(const BlockWorld3D &world,
                               const Vec3 &eyePosition,
                               const Vec3 &desiredMotion,
                               PlayerCollisionBounds bounds,
                               bool protectGroundEdge) {
  const double maximumMotion =
      std::max({std::abs(desiredMotion.x), std::abs(desiredMotion.y),
                std::abs(desiredMotion.z)});
  const int steps = std::max(
      1,
      static_cast<int>(std::ceil(maximumMotion / playerMaximumStepDistance)));
  const Vec3 stepMotion = desiredMotion * (1.0 / static_cast<double>(steps));
  Vec3 acceptedTotal{};
  Vec3 currentEye = eyePosition;
  for (int step = 0; step < steps; ++step) {
    const Vec3 accepted =
        resolveStep(world, currentEye, stepMotion, bounds, protectGroundEdge);
    acceptedTotal += accepted;
    currentEye += accepted;
  }
  return acceptedTotal;
}

void updateSneakPose(Camera3D &camera, const BlockWorld3D &world,
                     PlayerMotionState3D &state, bool sneakInput) {
  if (sneakInput && !state.sneaking) {
    camera.position.y -= playerSneakEyeDrop;
    state.sneaking = true;
    return;
  }
  if (!sneakInput && state.sneaking) {
    Vec3 standingEye = camera.position;
    standingEye.y += playerSneakEyeDrop;
    if (!playerCollidesAt(world, standingEye, playerCollisionBounds)) {
      camera.position = standingEye;
      state.sneaking = false;
    }
  }
}

} // namespace

BlockCoord3D playerLowerBodyBlock(const Vec3 &eyePosition,
                                  PlayerCollisionBounds bounds) {
  const Vec3 lowerBodyPoint{
      eyePosition.x, eyePosition.y - bounds.eyeToFeet + playerCollisionEpsilon,
      eyePosition.z};
  return containingBlock(lowerBodyPoint);
}

bool playerCollidesAt(const BlockWorld3D &world, const Vec3 &eyePosition,
                      PlayerCollisionBounds bounds) {
  const int minimumX = blockMinimum(eyePosition.x - bounds.radius);
  const int minimumY = blockMinimum(eyePosition.y - bounds.eyeToFeet);
  const int minimumZ = blockMinimum(eyePosition.z - bounds.radius);
  const int maximumX = blockMaximum(eyePosition.x + bounds.radius);
  const int maximumY = blockMaximum(eyePosition.y + bounds.eyeToHead);
  const int maximumZ = blockMaximum(eyePosition.z + bounds.radius);
  for (int z = minimumZ; z <= maximumZ; ++z) {
    for (int y = minimumY; y <= maximumY; ++y) {
      for (int x = minimumX; x <= maximumX; ++x) {
        if (world.isSolid({x, y, z})) {
          return true;
        }
      }
    }
  }
  return false;
}

Vec3 resolvePlayerMotion(const BlockWorld3D &world, const Vec3 &eyePosition,
                         const Vec3 &desiredMotion,
                         PlayerCollisionBounds bounds) {
  return resolveWithEdgeProtection(world, eyePosition, desiredMotion, bounds,
                                   false);
}

void updatePlayerMotion(Camera3D &camera, const BlockWorld3D &world,
                        PlayerMotionState3D &state, PlayerMoveInput3D moveInput,
                        bool jumpInput, double deltaSeconds) {
  deltaSeconds = std::clamp(deltaSeconds, 0.0, playerMaximumDeltaSeconds);
  updateSneakPose(camera, world, state, moveInput.sneak);
  const PlayerCollisionBounds activeBounds =
      state.sneaking ? playerSneakCollisionBounds : playerCollisionBounds;
  const double movementSpeed =
      playerWalkSpeed * (state.sneaking ? playerSneakSpeedMultiplier : 1.0);
  const Vec3 planarMotion = (camera.movementForward() * moveInput.forward +
                             camera.right() * moveInput.strafe) *
                            (movementSpeed * deltaSeconds);
  const double maximumMotion = std::max(
      {std::abs(planarMotion.x), std::abs(planarMotion.z),
       std::abs((state.verticalVelocity - playerGravity * deltaSeconds) *
                deltaSeconds)});
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
    const Vec3 desiredMotion{planarMotion.x / static_cast<double>(steps),
                             state.verticalVelocity * stepDelta,
                             planarMotion.z / static_cast<double>(steps)};
    const Vec3 acceptedMotion = resolveWithEdgeProtection(
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
