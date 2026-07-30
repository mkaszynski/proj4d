#include "proj4d/player_motion.hpp"

#include <algorithm>
#include <cmath>

namespace proj4d {

namespace {

constexpr double blockMaximumEpsilon = 0.0001;
constexpr double groundProbeDistance = 0.04;
constexpr double maximumStepDistance = 0.25;
constexpr double maximumDeltaSeconds = 0.05;

int blockMinimum(double value) { return static_cast<int>(std::floor(value)); }

int blockMaximum(double value) {
  return static_cast<int>(std::floor(value - blockMaximumEpsilon));
}

bool hasGroundContact(const BlockWorld &world, const Vec4 &eyePosition,
                      PlayerCollisionBounds bounds) {
  Vec4 probe = eyePosition;
  probe.y -= groundProbeDistance;
  return playerCollidesAt(world, probe, bounds);
}

Vec4 resolvePlayerMotionStep(const BlockWorld &world, const Vec4 &eyePosition,
                             const Vec4 &desiredMotion,
                             PlayerCollisionBounds bounds,
                             bool protectGroundEdge) {
  Vec4 accepted{};
  for (std::size_t axis = 0; axis < 4; ++axis) {
    Vec4 axisMotion{};
    axisMotion[axis] = desiredMotion[axis];
    const Vec4 candidate = eyePosition + accepted + axisMotion;
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

Vec4 resolvePlayerMotionWithEdgeProtection(const BlockWorld &world,
                                           const Vec4 &eyePosition,
                                           const Vec4 &desiredMotion,
                                           PlayerCollisionBounds bounds,
                                           bool protectGroundEdge) {
  const double maximumMotion =
      std::max({std::abs(desiredMotion.x), std::abs(desiredMotion.y),
                std::abs(desiredMotion.z), std::abs(desiredMotion.w)});
  const int steps = std::max(
      1, static_cast<int>(std::ceil(maximumMotion / maximumStepDistance)));
  const Vec4 stepMotion = desiredMotion * (1.0 / static_cast<double>(steps));
  Vec4 acceptedTotal{};
  Vec4 currentEye = eyePosition;
  for (int step = 0; step < steps; ++step) {
    const Vec4 accepted = resolvePlayerMotionStep(world, currentEye, stepMotion,
                                                  bounds, protectGroundEdge);
    acceptedTotal += accepted;
    currentEye += accepted;
  }
  return acceptedTotal;
}

void updateSneakPose(Camera4D &camera, const BlockWorld &world,
                     PlayerMotionState &state, bool sneakInput) {
  if (sneakInput && !state.sneaking) {
    camera.position.y -= playerSneakEyeDrop;
    state.sneaking = true;
    return;
  }
  if (!sneakInput && state.sneaking) {
    Vec4 standingEye = camera.position;
    standingEye.y += playerSneakEyeDrop;
    if (!playerCollidesAt(world, standingEye, playerCollisionBounds)) {
      camera.position = standingEye;
      state.sneaking = false;
    }
  }
}

} // namespace

BlockCoord playerLowerBodyBlock(const Vec4 &eyePosition,
                                PlayerCollisionBounds bounds) {
  return containingBlock({
      eyePosition.x,
      eyePosition.y - bounds.eyeToFeet + blockMaximumEpsilon,
      eyePosition.z,
      eyePosition.w,
  });
}

bool playerCollidesAt(const BlockWorld &world, const Vec4 &eyePosition,
                      PlayerCollisionBounds bounds) {
  const int minimumX = blockMinimum(eyePosition.x - bounds.radius);
  const int minimumY = blockMinimum(eyePosition.y - bounds.eyeToFeet);
  const int minimumZ = blockMinimum(eyePosition.z - bounds.radius);
  const int minimumW = blockMinimum(eyePosition.w - bounds.radius);
  const int maximumX = blockMaximum(eyePosition.x + bounds.radius);
  const int maximumY = blockMaximum(eyePosition.y + bounds.eyeToHead);
  const int maximumZ = blockMaximum(eyePosition.z + bounds.radius);
  const int maximumW = blockMaximum(eyePosition.w + bounds.radius);

  for (int w = minimumW; w <= maximumW; ++w) {
    for (int y = minimumY; y <= maximumY; ++y) {
      for (int z = minimumZ; z <= maximumZ; ++z) {
        for (int x = minimumX; x <= maximumX; ++x) {
          if (world.isSolid({x, y, z, w})) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool playerCanOccupy(const BlockWorld &world, const Vec4 &eyePosition) {
  return !playerCollidesAt(world, eyePosition);
}

Vec4 resolvePlayerMotion(const BlockWorld &world, const Vec4 &eyePosition,
                         const Vec4 &desiredMotion,
                         PlayerCollisionBounds bounds) {
  return resolvePlayerMotionWithEdgeProtection(world, eyePosition,
                                               desiredMotion, bounds, false);
}

void updatePlayerMotion(Camera4D &camera, const BlockWorld &world,
                        PlayerMotionState &state, PlayerMoveInput moveInput,
                        bool jumpInput, double deltaSeconds) {
  deltaSeconds = std::clamp(deltaSeconds, 0.0, maximumDeltaSeconds);
  updateSneakPose(camera, world, state, moveInput.sneak);
  const PlayerCollisionBounds activeBounds =
      state.sneaking ? playerSneakCollisionBounds : playerCollisionBounds;
  const double movementSpeed =
      playerWalkSpeed * (state.sneaking ? playerSneakSpeedMultiplier : 1.0);
  const Vec4 requestedDirection =
      camera.flattenedForward() * moveInput.forward +
      camera.ordinarySideways() * moveInput.ordinaryStrafe +
      camera.fourthSideways() * moveInput.fourthStrafe;
  const Vec4 planarMotion = requestedDirection * (movementSpeed * deltaSeconds);
  const double maximumMotion = std::max(
      {std::abs(planarMotion.x), std::abs(planarMotion.z),
       std::abs(planarMotion.w),
       std::abs((state.verticalVelocity - playerGravity * deltaSeconds) *
                deltaSeconds)});
  const int steps = std::max(
      1, static_cast<int>(std::ceil(maximumMotion / maximumStepDistance)));

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
    const Vec4 desiredMotion{
        planarMotion.x / static_cast<double>(steps),
        state.verticalVelocity * stepDelta,
        planarMotion.z / static_cast<double>(steps),
        planarMotion.w / static_cast<double>(steps),
    };
    const Vec4 acceptedMotion = resolvePlayerMotionWithEdgeProtection(
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
