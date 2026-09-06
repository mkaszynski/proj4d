#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/camera2d.hpp"
#include "proj4d/camera3d.hpp"
#include "proj4d/chunk.hpp"
#include "proj4d/mouse_input.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/player_motion2d.hpp"
#include "proj4d/player_motion3d.hpp"
#include "proj4d/render2d.hpp"
#include "proj4d/render3d.hpp"
#include "proj4d/render_geometry.hpp"
#include "proj4d/terrain_generator.hpp"
#include "proj4d/view_status.hpp"
#include "proj4d/world.hpp"
#include "proj4d/world2d.hpp"
#include "proj4d/world3d.hpp"
#include "proj4d/world_save.hpp"

namespace {

int failures = 0;

class TemporaryDirectory {
public:
  TemporaryDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("proj4d-save-test-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

double totalLineLength(const std::vector<proj4d::Line3> &lines) {
  double total = 0.0;
  for (const proj4d::Line3 &line : lines) {
    total += proj4d::length(line.to - line.from);
  }
  return total;
}

void testCameraProjectionAndRotation() {
  proj4d::Camera4D camera;
  camera.position = {};
  const auto center = camera.project({0.0, 0.0, 2.0, 0.0});
  expect(center.has_value(), "point in front of 4D camera projects");
  expect(center && proj4d::nearlyEqual(center->position.x, 0.0),
         "center x is zero");
  expect(center && proj4d::nearlyEqual(center->position.y, 0.0),
         "center y is zero");
  expect(center && proj4d::nearlyEqual(center->position.z, 0.0),
         "center z is zero");

  camera.turnHorizontal(0.31);
  camera.turnVertical(-0.27);
  camera.turnFourth(0.43);
  const std::array<proj4d::Vec4, 4> basis{
      camera.imageX,
      camera.imageY,
      camera.imageZ,
      camera.forward,
  };
  for (std::size_t left = 0; left < basis.size(); ++left) {
    expect(proj4d::nearlyEqual(proj4d::length(basis[left]), 1.0, 1.0e-8),
           "4D camera basis remains normalized");
    for (std::size_t right = left + 1; right < basis.size(); ++right) {
      expect(proj4d::nearlyEqual(proj4d::dot(basis[left], basis[right]), 0.0,
                                 1.0e-8),
             "4D camera basis remains orthogonal");
    }
  }
}

void testVerticalLookStopsAtStraightUpAndDown() {
  proj4d::Camera4D camera;
  camera.turnVertical(10.0);
  expect(proj4d::nearlyEqual(camera.verticalPitch(),
                             proj4d::straightVerticalPitch),
         "vertical look stops at straight up");
  expect(proj4d::nearlyEqual(camera.forward.y, 1.0),
         "straight-up view points along the world vertical axis");

  camera.turnVertical(1.0);
  camera.turnHorizontal(0.4);
  camera.turnFourth(-0.3);
  expect(proj4d::nearlyEqual(camera.verticalPitch(),
                             proj4d::straightVerticalPitch),
         "additional and horizontal turns cannot cross the upper pitch limit");

  camera.turnVertical(-10.0);
  expect(proj4d::nearlyEqual(camera.verticalPitch(),
                             -proj4d::straightVerticalPitch),
         "vertical look stops at straight down");
  expect(proj4d::nearlyEqual(camera.forward.y, -1.0),
         "straight-down view points against the world vertical axis");

  const std::array<proj4d::Vec4, 4> basis{
      camera.imageX,
      camera.imageY,
      camera.imageZ,
      camera.forward,
  };
  for (std::size_t left = 0; left < basis.size(); ++left) {
    expect(proj4d::nearlyEqual(proj4d::length(basis[left]), 1.0, 1.0e-8),
           "pitch-limited camera basis remains normalized");
    for (std::size_t right = left + 1; right < basis.size(); ++right) {
      expect(proj4d::nearlyEqual(proj4d::dot(basis[left], basis[right]), 0.0,
                                 1.0e-8),
             "pitch-limited camera basis remains orthogonal");
    }
  }
}

void testHorizontalLookKeepsTheViewLevel() {
  proj4d::Camera4D camera;
  camera.turnVertical(0.7);
  const double verticalComponent = camera.forward.y;
  camera.turnHorizontal(0.8);
  camera.turnHorizontal(0.8);

  expect(proj4d::nearlyEqual(camera.verticalPitch(), 0.7),
         "left and right look preserve the vertical viewing angle");
  expect(proj4d::nearlyEqual(camera.forward.y, verticalComponent),
         "left and right look follow a level circle");
  expect(proj4d::nearlyEqual(camera.imageX.y, 0.0),
         "left and right look do not roll or tilt the horizon");
}

void testMouseMovementTurnsInOrdinaryAndFourthDimensions() {
  const proj4d::MouseMotionMapping worldMotion = proj4d::mapMouseMotion(
      25.0, -50.0, proj4d::MouseMotionMode::FourthDimensionalLook);
  proj4d::Camera4D camera;
  camera.turnHorizontal(worldMotion.worldHorizontalTurn);
  camera.turnVertical(worldMotion.worldVerticalTurn);
  camera.turnFourth(worldMotion.worldFourthTurn);

  expect(proj4d::nearlyEqual(camera.horizontalAngle(), 0.1, 1.0e-8),
         "rightward mouse movement turns ordinary horizontal look");
  expect(proj4d::nearlyEqual(camera.fourthAngle(), -0.2, 1.0e-8),
         "upward mouse movement turns fourth-dimensional look");
  expect(proj4d::nearlyEqual(camera.verticalPitch(), 0.0),
         "ordinary mouse look does not change vertical pitch");
  expect(proj4d::nearlyEqual(worldMotion.visionCubeYawTurn, 0.0) &&
             proj4d::nearlyEqual(worldMotion.visionCubePitchTurn, 0.0),
         "unmodified mouse movement does not orbit the vision cube");

  const proj4d::MouseMotionMapping controlMotion = proj4d::mapMouseMotion(
      25.0, -50.0, proj4d::MouseMotionMode::VisionCubeOrbit);
  expect(proj4d::nearlyEqual(controlMotion.worldHorizontalTurn, 0.0) &&
             proj4d::nearlyEqual(controlMotion.worldVerticalTurn, 0.0) &&
             proj4d::nearlyEqual(controlMotion.worldFourthTurn, 0.0),
         "Ctrl plus mouse leaves the 4D world look unchanged");
  expect(
      proj4d::nearlyEqual(controlMotion.visionCubeYawTurn, 0.1, 1.0e-8) &&
          proj4d::nearlyEqual(controlMotion.visionCubePitchTurn, -0.2, 1.0e-8),
      "Ctrl plus mouse retains the original vision-cube orbit");

  const proj4d::MouseMotionMapping tabMotion = proj4d::mapMouseMotion(
      25.0, -50.0, proj4d::MouseMotionMode::VerticalLook);
  proj4d::Camera4D tabCamera;
  tabCamera.turnHorizontal(tabMotion.worldHorizontalTurn);
  tabCamera.turnVertical(tabMotion.worldVerticalTurn);
  tabCamera.turnFourth(tabMotion.worldFourthTurn);
  expect(proj4d::nearlyEqual(tabCamera.horizontalAngle(), 0.0),
         "Tab plus horizontal mouse movement cannot turn sideways");
  expect(proj4d::nearlyEqual(tabCamera.verticalPitch(), 0.2, 1.0e-8),
         "Tab plus upward mouse movement looks upward");
  expect(proj4d::nearlyEqual(tabCamera.fourthAngle(), 0.0),
         "Tab allows only vertical world look");
  expect(proj4d::selectMouseMotionMode(false, true) ==
             proj4d::MouseMotionMode::VerticalLook,
         "Tab selects vertical mouse look without Ctrl");
  expect(proj4d::selectMouseMotionMode(true, true) ==
             proj4d::MouseMotionMode::VisionCubeOrbit,
         "Ctrl takes priority over Tab for mouse control");
}

void testCameraProvidesTwoOrthogonalSidewaysDirections() {
  proj4d::Camera4D camera;
  camera.turnHorizontal(0.47);
  camera.turnFourth(-0.31);
  camera.turnVertical(0.62);

  const proj4d::Vec4 forward = camera.flattenedForward();
  const proj4d::Vec4 ordinarySideways = camera.ordinarySideways();
  const proj4d::Vec4 fourthSideways = camera.fourthSideways();
  expect(proj4d::nearlyEqual(proj4d::length(ordinarySideways), 1.0, 1.0e-8) &&
             proj4d::nearlyEqual(proj4d::length(fourthSideways), 1.0, 1.0e-8),
         "both sideways movement directions remain normalized");
  expect(proj4d::nearlyEqual(proj4d::dot(forward, ordinarySideways), 0.0,
                             1.0e-8) &&
             proj4d::nearlyEqual(proj4d::dot(forward, fourthSideways), 0.0,
                                 1.0e-8),
         "both sideways directions remain perpendicular to W/S movement");
  expect(proj4d::nearlyEqual(proj4d::dot(ordinarySideways, fourthSideways), 0.0,
                             1.0e-8),
         "A/D and Q/E movement directions remain perpendicular to each other");
  expect(proj4d::nearlyEqual(ordinarySideways.y, 0.0) &&
             proj4d::nearlyEqual(fourthSideways.y, 0.0),
         "sideways controls remain level when the view looks up or down");
}

void testViewStatusReportsFourCoordinatesAndThreeAngles() {
  proj4d::Camera4D camera;
  camera.position = {-12.25, 3.5, 42.0, 0.75};
  camera.turnHorizontal(0.5);
  camera.turnVertical(-0.25);
  camera.turnFourth(0.75);

  expect(proj4d::nearlyEqual(camera.horizontalAngle(), 0.5),
         "camera reports its ordinary horizontal angle");
  expect(proj4d::nearlyEqual(camera.verticalPitch(), -0.25),
         "camera reports its vertical angle");
  expect(proj4d::nearlyEqual(camera.fourthAngle(), 0.75),
         "camera reports its fourth-dimensional angle");
  const double horizontal = camera.horizontalAngle();
  const double fourth = camera.fourthAngle();
  const proj4d::Vec4 reconstructedForward{
      std::cos(fourth) * std::sin(horizontal),
      0.0,
      std::cos(fourth) * std::cos(horizontal),
      std::sin(fourth),
  };
  const proj4d::Vec4 actualForward = camera.flattenedForward();
  for (std::size_t axis = 0; axis < 4; ++axis) {
    expect(proj4d::nearlyEqual(reconstructedForward[axis], actualForward[axis],
                               1.0e-8),
           "reported angles reconstruct the actual horizontal view");
  }
  expect(proj4d::formatViewStatus(camera) ==
             "X:-12.25 Y:3.50 Z:42.00 W:0.75 | H:28.6 V:-14.3 4D:43.0",
         "HUD status formats four coordinates and three angles in degrees");
}

void testGroundedPlayerCanMoveWithoutJumping() {
  proj4d::BlockWorld world(777U);
  const proj4d::BlockCoord ground{0, 2, 0, 0};
  static_cast<void>(world.setSolid(ground, true));
  static_cast<void>(world.setSolid({0, 3, 0, 0}, false));
  static_cast<void>(world.setSolid({0, 4, 0, 0}, false));
  expect(world.isSolid(ground), "movement fixture has solid ground");
  expect(!world.isSolid({0, 3, 0, 0}),
         "movement fixture has clear lower body space");
  expect(!world.isSolid({0, 4, 0, 0}), "movement fixture has clear eye space");

  proj4d::Camera4D camera;
  camera.position = {
      0.25,
      static_cast<double>(ground.y + 1) + proj4d::playerEyeHeight,
      0.25,
      0.25,
  };
  const proj4d::Vec4 spawnPosition = camera.position;
  proj4d::PlayerMotionState motion{0.0, true, spawnPosition};

  expect(proj4d::playerLowerBodyBlock(camera.position) ==
             proj4d::BlockCoord{0, 3, 0, 0},
         "standing collision probe stays above the supporting block");
  expect(proj4d::playerCanOccupy(world, camera.position),
         "grounded player position is not mistaken for a collision");

  proj4d::updatePlayerMotion(camera, world, motion, {1.0, 0.0, 0.0}, false,
                             0.1);
  expect(camera.position.z > spawnPosition.z,
         "grounded player moves forward without jumping");
  expect(motion.grounded, "player remains grounded after horizontal movement");
  expect(proj4d::nearlyEqual(camera.position.y, spawnPosition.y),
         "ground contact preserves standing eye height");
}

void testPlayerPhysicsMatchesHypercraft() {
  expect(proj4d::nearlyEqual(proj4d::playerWalkSpeed, 7.0),
         "walking speed matches Hypercraft");
  expect(proj4d::nearlyEqual(proj4d::playerGravity, 36.0),
         "gravity matches Hypercraft");
  expect(proj4d::nearlyEqual(proj4d::playerJumpHeight, 1.5),
         "configured jump height matches Hypercraft");
  expect(proj4d::nearlyEqual(proj4d::playerCollisionBounds.radius * 2.0, 0.3),
         "the player is 0.3 blocks wide on x, z, and w like Hypercraft");
  expect(proj4d::nearlyEqual(proj4d::playerCollisionBounds.eyeToFeet, 1.65),
         "eye-to-feet collision height matches Hypercraft");
  expect(proj4d::nearlyEqual(proj4d::playerCollisionBounds.eyeToHead, 0.18),
         "eye-to-head collision height matches Hypercraft");

  proj4d::BlockWorld flatWorld(proj4d::TerrainMode::Flat, 2026U);
  proj4d::Camera4D camera;
  camera.position = {0.5, 1.0 + proj4d::playerEyeHeight, 0.5, 0.5};
  const proj4d::Vec4 start = camera.position;
  proj4d::PlayerMotionState motion{0.0, true, start};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(camera, flatWorld, motion, {1.0, 0.0, 0.0},
                               false, 1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(camera.position.z - start.z, 7.0, 1.0e-8),
         "one second of forward input travels seven blocks like Hypercraft");

  proj4d::Camera4D clampedCamera;
  clampedCamera.position = start;
  proj4d::PlayerMotionState clampedMotion{0.0, true, start};
  proj4d::updatePlayerMotion(clampedCamera, flatWorld, clampedMotion,
                             {1.0, 0.0, 0.0}, false, 1.0);
  expect(proj4d::nearlyEqual(clampedCamera.position.z - start.z, 0.35, 1.0e-8),
         "a long frame uses Hypercraft's 50 millisecond physics cap");

  proj4d::Camera4D strafeCamera;
  strafeCamera.position = start;
  proj4d::PlayerMotionState strafeMotion{0.0, true, start};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(strafeCamera, flatWorld, strafeMotion,
                               {0.0, 1.0, 1.0}, false, 1.0 / 60.0);
  }
  expect(
      proj4d::nearlyEqual(strafeCamera.position.x - start.x, 7.0, 1.0e-8) &&
          proj4d::nearlyEqual(strafeCamera.position.w - start.w, 7.0, 1.0e-8),
      "A/D and Q/E each use Hypercraft's seven-block movement speed");
  expect(proj4d::nearlyEqual(strafeCamera.position.z, start.z, 1.0e-8),
         "pure sideways input does not add forward movement");

  proj4d::BlockWorld collisionWorld(proj4d::TerrainMode::Flat, 2027U);
  static_cast<void>(collisionWorld.setSolid({0, 100, 0, 1}, true));
  expect(!proj4d::playerCollidesAt(collisionWorld, {0.5, 100.5, 0.5, 0.84}),
         "the narrow 4D body can approach a neighboring w block");
  expect(proj4d::playerCollidesAt(collisionWorld, {0.5, 100.5, 0.5, 0.86}),
         "the 4D body collides after its radius reaches a neighboring w block");
}

void testPlayerSneaksLikeMinecraft() {
  expect(proj4d::nearlyEqual(proj4d::playerSneakSpeedMultiplier, 0.3),
         "sneaking uses Minecraft's 30-percent movement multiplier");
  expect(proj4d::nearlyEqual(proj4d::playerSneakCollisionBounds.eyeToFeet,
                             proj4d::playerCollisionBounds.eyeToFeet -
                                 proj4d::playerSneakEyeDrop),
         "the sneaking collision body keeps its feet fixed as the eye lowers");

  proj4d::BlockWorld flatWorld(proj4d::TerrainMode::Flat, 3030U);
  proj4d::Camera4D speedCamera;
  speedCamera.position = {0.5, 1.0 + proj4d::playerEyeHeight, 0.5, 0.5};
  const proj4d::Vec4 standingPosition = speedCamera.position;
  proj4d::PlayerMotionState speedMotion{0.0, true, standingPosition};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(speedCamera, flatWorld, speedMotion,
                               {1.0, 0.0, 0.0, true}, false, 1.0 / 60.0);
  }
  expect(speedMotion.sneaking,
         "holding Shift keeps the player in the sneak pose");
  expect(proj4d::nearlyEqual(speedCamera.position.z - standingPosition.z, 2.1,
                             1.0e-8),
         "one second of sneaking moves at 30 percent of walking speed");
  expect(proj4d::nearlyEqual(speedCamera.position.y,
                             standingPosition.y - proj4d::playerSneakEyeDrop),
         "sneaking lowers the viewpoint without moving the player's feet");
  expect(proj4d::playerLowerBodyBlock(speedCamera.position,
                                      proj4d::playerSneakCollisionBounds) ==
             proj4d::BlockCoord{0, 1, 2, 0},
         "the lowered sneak pose retains the correct lower-body block");

  proj4d::updatePlayerMotion(speedCamera, flatWorld, speedMotion, {}, false,
                             1.0 / 60.0);
  expect(!speedMotion.sneaking,
         "releasing Shift returns to standing when headroom is clear");
  expect(proj4d::nearlyEqual(speedCamera.position.y, standingPosition.y),
         "releasing Shift restores the standing viewpoint");

  proj4d::BlockWorld lowWorld(proj4d::TerrainMode::Low);
  proj4d::Camera4D lowCamera;
  const int lowSurface = lowWorld.surfaceHeightAt(0, 0, 0);
  lowCamera.position = {
      0.5,
      static_cast<double>(lowSurface + 1) + proj4d::playerEyeHeight,
      0.5,
      0.5,
  };
  const proj4d::Vec4 lowStart = lowCamera.position;
  proj4d::PlayerMotionState lowMotion{0.0, true, lowStart};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(lowCamera, lowWorld, lowMotion,
                               {1.0, 0.0, 0.0, true}, false, 1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(lowCamera.position.z - lowStart.z, 2.1, 1.0e-8),
         "held-Shift movement works at Low's elevated spawn height");
  expect(!proj4d::playerCollidesAt(lowWorld, lowCamera.position,
                                   proj4d::playerSneakCollisionBounds),
         "the lowered Low-world pose does not numerically enter the ground");

  proj4d::BlockWorld ledgeWorld(proj4d::TerrainMode::Flat, 3031U);
  for (int w = 0; w <= 3; ++w) {
    for (int z = 0; z <= 3; ++z) {
      for (int x = 0; x <= 3; ++x) {
        if (x != 0 || z != 0 || w != 0) {
          static_cast<void>(ledgeWorld.setSolid({x, 0, z, w}, false));
        }
      }
    }
  }
  proj4d::Camera4D ledgeCamera;
  ledgeCamera.position = standingPosition;
  proj4d::PlayerMotionState ledgeMotion{0.0, true, standingPosition};
  for (int frame = 0; frame < 120; ++frame) {
    proj4d::updatePlayerMotion(ledgeCamera, ledgeWorld, ledgeMotion,
                               {1.0, 1.0, 1.0, true}, false, 1.0 / 60.0);
  }
  expect(ledgeMotion.grounded,
         "a sneaking player remains supported at a 4D platform corner");
  expect(ledgeCamera.position.x < 1.16 && ledgeCamera.position.z < 1.16 &&
             ledgeCamera.position.w < 1.16,
         "sneaking prevents walking off x, z, and w edges");

  proj4d::Camera4D walkingCamera;
  walkingCamera.position = standingPosition;
  proj4d::PlayerMotionState walkingMotion{0.0, true, standingPosition};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(walkingCamera, ledgeWorld, walkingMotion,
                               {1.0, 1.0, 1.0, false}, false, 1.0 / 60.0);
  }
  expect(walkingCamera.position.x > 1.16 && walkingCamera.position.z > 1.16 &&
             walkingCamera.position.w > 1.16,
         "without Shift, ordinary movement can leave the platform");
  expect(walkingCamera.position.y < standingPosition.y,
         "walking beyond the unsupported edge allows the player to fall");

  proj4d::BlockWorld headroomWorld(proj4d::TerrainMode::Flat, 3032U);
  static_cast<void>(headroomWorld.setSolid({0, 3, 0, 0}, true));
  proj4d::Camera4D headroomCamera;
  headroomCamera.position = {0.5, 2.55, 0.5, 0.5};
  proj4d::PlayerMotionState headroomMotion{0.0, false, headroomCamera.position,
                                           true};
  proj4d::updatePlayerMotion(headroomCamera, headroomWorld, headroomMotion, {},
                             false, 0.0);
  expect(headroomMotion.sneaking,
         "the player stays crouched when a ceiling blocks standing");
  static_cast<void>(headroomWorld.setSolid({0, 3, 0, 0}, false));
  proj4d::updatePlayerMotion(headroomCamera, headroomWorld, headroomMotion, {},
                             false, 0.0);
  expect(!headroomMotion.sneaking,
         "the player stands after releasing Shift once headroom is clear");
}

void testPlayerSlidesAlongBlockedFaces() {
  proj4d::BlockWorld world(31337U);
  for (int y = 2; y <= 4; ++y) {
    for (int x = 0; x <= 1; ++x) {
      static_cast<void>(world.setSolid({x, y, 0, 0}, false));
    }
  }
  static_cast<void>(world.setSolid({0, 2, 0, 0}, true));
  static_cast<void>(world.setSolid({1, 3, 0, 0}, true));
  static_cast<void>(world.setSolid({1, 4, 0, 0}, true));

  proj4d::Camera4D camera;
  camera.position = {
      0.75,
      3.0 + proj4d::playerEyeHeight,
      0.25,
      0.25,
  };
  camera.turnHorizontal(proj4d::straightVerticalPitch * 0.5);
  const proj4d::Vec4 spawnPosition = camera.position;
  proj4d::PlayerMotionState motion{0.0, true, spawnPosition};

  proj4d::updatePlayerMotion(camera, world, motion, {1.0, 0.0, 0.0}, false,
                             0.2);
  expect(proj4d::nearlyEqual(camera.position.x, spawnPosition.x),
         "blocked movement component stops at the wall");
  expect(camera.position.z > spawnPosition.z,
         "free movement component slides along the wall");
}

void testPlayerJumpsOneAndAHalfBlocks() {
  proj4d::BlockWorld world(888U);
  const proj4d::BlockCoord ground{0, 2, 0, 0};
  static_cast<void>(world.setSolid(ground, true));
  for (int y = 3; y <= 6; ++y) {
    static_cast<void>(world.setSolid({0, y, 0, 0}, false));
  }

  proj4d::Camera4D camera;
  camera.position = {
      0.25,
      static_cast<double>(ground.y + 1) + proj4d::playerEyeHeight,
      0.25,
      0.25,
  };
  const proj4d::Vec4 spawnPosition = camera.position;
  proj4d::PlayerMotionState motion{0.0, true, spawnPosition};

  double maximumEyeY = camera.position.y;
  proj4d::updatePlayerMotion(camera, world, motion, {}, true, 1.0 / 120.0);
  expect(proj4d::nearlyEqual(
             motion.verticalVelocity,
             std::sqrt(2.0 * proj4d::playerGravity * proj4d::playerJumpHeight) -
                 proj4d::playerGravity / 120.0,
             1.0e-8),
         "jump launch velocity and first gravity step match Hypercraft");
  maximumEyeY = std::max(maximumEyeY, camera.position.y);
  for (int step = 1; step < 240 && !motion.grounded; ++step) {
    proj4d::updatePlayerMotion(camera, world, motion, {}, false, 1.0 / 120.0);
    maximumEyeY = std::max(maximumEyeY, camera.position.y);
  }
  expect(proj4d::nearlyEqual(maximumEyeY - spawnPosition.y,
                             proj4d::playerJumpHeight, 0.16),
         "jump apex follows Hypercraft's 1.5-block jump configuration");
  expect(motion.grounded, "jump finishes by landing on the ground");
  expect(proj4d::nearlyEqual(camera.position.y, spawnPosition.y, 0.04),
         "landing returns within Hypercraft's ground-contact probe");
}

void testTrueFourDimensionalChunks() {
  expect(proj4d::chunkSize == 16, "each chunk axis is 16 blocks");
  expect(proj4d::chunkVolume == 16 * 16 * 16 * 16,
         "chunks contain exactly 16x16x16x16 tesseracts");
  expect(proj4d::chunkCoordForBlock({-1, -17, 16, -33}) ==
             proj4d::ChunkCoord{-1, -2, 1, -3},
         "negative 4D coordinates use floor division");
  expect(proj4d::localCoordForBlock({-1, -17, 16, -33}) ==
             proj4d::LocalBlockCoord{15, 15, 0, 15},
         "negative 4D coordinates map to positive chunk-local coordinates");
  expect(proj4d::localBlockIndex({15, 15, 15, 15}) ==
             static_cast<std::size_t>(proj4d::chunkVolume - 1),
         "all four local axes contribute to chunk storage");

  proj4d::Chunk chunk({2, -1, 4, -3});
  chunk.setSolid({15, 2, 9, 7}, true);
  expect(chunk.isSolid({15, 2, 9, 7}),
         "chunk stores a block at a full 4D local coordinate");
  expect(!chunk.isSolid({15, 2, 9, 6}),
         "neighboring w coordinate remains independent");
}

void testTrueTwoDimensionalWorldAndChunks() {
  expect(proj4d::blockCoord4D(proj4d::BlockCoord2D{-1, -17}) ==
             proj4d::BlockCoord{-1, -17, 0, 0},
         "2D coordinates map exactly onto the z=0,w=0 4D slice");

  constexpr std::array modes{
      proj4d::TerrainMode::Flat,
      proj4d::TerrainMode::Low,
      proj4d::TerrainMode::Density,
  };
  constexpr std::array<int, 7> xCoordinates{-64, -17, 0, 8, 31, 64, 129};
  for (const proj4d::TerrainMode mode : modes) {
    proj4d::BlockWorld world4D(mode, 2468U, 4U);
    proj4d::BlockWorld2D world2D(world4D);
    for (const int x : xCoordinates) {
      expect(world2D.surfaceHeightAt(x) == world4D.surfaceHeightAt(x, 0, 0),
             "2D terrain is the exact z=0,w=0 cross-section of its 4D mode");
      for (int y = -8; y <= 32; y += 4) {
        expect(world2D.generatedSolidAt({x, y}) ==
                   world4D.generatedSolidAt({x, y, 0, 0}),
               "2D terrain solidity matches the selected 4D generator");
      }
    }
  }

  proj4d::BlockWorld sharedWorld(proj4d::TerrainMode::Low, 2468U, 2U);
  proj4d::BlockWorld2D world(sharedWorld);
  static_cast<void>(world.isSolid({0, 0}));
  static_cast<void>(world.isSolid({32, 0}));
  static_cast<void>(world.isSolid({64, 0}));
  expect(world.loadedChunkCount() == sharedWorld.loadedChunkCount() &&
             world.loadedChunkCount() <= world.maximumLoadedChunks(),
         "2D uses the same bounded 16x16x16x16 chunk cache as 4D");
  const proj4d::BlockCoord2D edit{1000000, 1000};
  const bool generated = world.generatedSolidAt(edit);
  expect(world.setSolid(edit, !generated), "a distant 2D block can be edited");
  expect(sharedWorld.isSolid({edit.x, edit.y, 0, 0}) == !generated,
         "a 2D edit immediately changes the corresponding 4D tesseract");
  static_cast<void>(world.isSolid({-96, 0}));
  expect(world.isSolid(edit) == !generated,
         "shared 4D edits survive chunk eviction and regeneration in 2D");
}

void testTrueThreeDimensionalSharedSlice() {
  expect(proj4d::blockCoord4D(proj4d::BlockCoord3D{-1, -17, 23}) ==
             proj4d::BlockCoord{-1, -17, 23, 0},
         "3D coordinates map exactly onto the w=0 4D slice");

  constexpr std::array modes{
      proj4d::TerrainMode::Flat,
      proj4d::TerrainMode::Low,
      proj4d::TerrainMode::Density,
  };
  for (const proj4d::TerrainMode mode : modes) {
    proj4d::BlockWorld world4D(mode, 2468U, 4U);
    proj4d::BlockWorld3D world3D(world4D);
    for (int z = -17; z <= 17; z += 17) {
      for (int x = -17; x <= 17; x += 17) {
        expect(world3D.surfaceHeightAt(x, z) ==
                   world4D.surfaceHeightAt(x, z, 0),
               "3D terrain is the exact w=0 cross-section of its 4D mode");
        for (int y = -8; y <= 32; y += 8) {
          expect(world3D.generatedSolidAt({x, y, z}) ==
                     world4D.generatedSolidAt({x, y, z, 0}),
                 "3D solidity delegates to the selected 4D generator");
        }
      }
    }
  }

  proj4d::BlockWorld sharedWorld(proj4d::TerrainMode::Low, 2468U, 2U);
  proj4d::BlockWorld3D world(sharedWorld);
  static_cast<void>(world.isSolid({0, 0, 0}));
  static_cast<void>(world.isSolid({32, 0, 32}));
  static_cast<void>(world.isSolid({64, 0, 64}));
  expect(world.loadedChunkCount() == sharedWorld.loadedChunkCount() &&
             world.loadedChunkCount() <= world.maximumLoadedChunks(),
         "3D uses the same bounded 16x16x16x16 chunk cache as 4D");
  const proj4d::BlockCoord3D edit{1000000, 1000, -1000000};
  const bool generated = world.generatedSolidAt(edit);
  expect(world.setSolid(edit, !generated), "a distant 3D cube can be edited");
  expect(sharedWorld.isSolid({edit.x, edit.y, edit.z, 0}) == !generated,
         "a 3D edit immediately changes the corresponding 4D tesseract");
}

void testSharedWorldSaving() {
  TemporaryDirectory temporaryDirectory;
  const std::filesystem::path flatPath =
      temporaryDirectory.path() /
      proj4d::worldSaveFilename(proj4d::TerrainMode::Flat);
  const std::filesystem::path lowPath =
      temporaryDirectory.path() /
      proj4d::worldSaveFilename(proj4d::TerrainMode::Low);
  const std::filesystem::path highPath =
      temporaryDirectory.path() /
      proj4d::worldSaveFilename(proj4d::TerrainMode::Density);
  expect(flatPath != lowPath && flatPath != highPath && lowPath != highPath,
         "Flat, Low, and High use three distinct persistent worlds");

  constexpr std::uint32_t seed = 2468U;
  proj4d::BlockWorld original(proj4d::TerrainMode::Low, seed, 4U);
  proj4d::BlockWorld2D slice(original);
  proj4d::BlockWorld3D slice3D(original);
  const proj4d::BlockCoord2D sliceEdit{7, 100};
  const proj4d::BlockCoord2D brokenSliceEdit{9, 0};
  const proj4d::BlockCoord outOfSliceEdit{8, 100, 3, -2};
  const proj4d::BlockCoord3D threeDimensionalEdit{12, 100, 4};
  expect(slice.setSolid(sliceEdit, true),
         "2D can add an edit to the shared 4D world");
  expect(slice.setSolid(brokenSliceEdit, false),
         "2D can remove a generated block from the shared 4D world");
  expect(original.setSolid(outOfSliceEdit, true),
         "4D can add an edit outside the thin 2D slice");
  expect(slice3D.setSolid(threeDimensionalEdit, true),
         "3D can add an edit to the shared w=0 world slice");

  std::string error;
  expect(proj4d::saveWorldSave(lowPath, original, error),
         "a shared terrain world saves successfully: " + error);
  std::filesystem::path temporarySavePath = lowPath;
  temporarySavePath += ".tmp";
  expect(std::filesystem::exists(lowPath) &&
             !std::filesystem::exists(temporarySavePath),
         "saving atomically installs the final file without a temporary file");

  proj4d::BlockWorld loaded(proj4d::TerrainMode::Low, seed, 4U);
  expect(proj4d::loadWorldSave(lowPath, loaded, error) ==
             proj4d::WorldLoadStatus::Loaded,
         "selecting the same terrain loads its existing world: " + error);
  proj4d::BlockWorld2D loadedSlice(loaded);
  proj4d::BlockWorld3D loadedSlice3D(loaded);
  expect(loadedSlice.isSolid(sliceEdit),
         "an edit made in 2D is present after loading the world in 4D");
  expect(!loadedSlice.isSolid(brokenSliceEdit),
         "a block broken in 2D remains absent after loading in 4D");
  expect(loaded.isSolid(outOfSliceEdit),
         "4D edits outside the 2D slice persist without becoming 2D blocks");
  expect(loadedSlice3D.isSolid(threeDimensionalEdit),
         "an edit made in 3D is present after loading the world in 4D");
  expect(!loadedSlice.isSolid({threeDimensionalEdit.x, threeDimensionalEdit.y}),
         "a z-nonzero 3D edit does not leak into the thin 2D slice");
  expect(loaded.edits().size() == 4U,
         "the save restores exactly the durable world edit overrides");

  expect(loaded.setSolid(proj4d::blockCoord4D(sliceEdit), false),
         "4D can modify the same tesseract previously edited through 2D");
  expect(proj4d::saveWorldSave(lowPath, loaded, error),
         "the updated shared world saves successfully: " + error);
  proj4d::BlockWorld reloaded(proj4d::TerrainMode::Low, seed, 4U);
  expect(proj4d::loadWorldSave(lowPath, reloaded, error) ==
             proj4d::WorldLoadStatus::Loaded,
         "the updated shared world reloads successfully: " + error);
  proj4d::BlockWorld2D reloadedSlice(reloaded);
  expect(!reloadedSlice.isSolid(sliceEdit),
         "a saved 4D edit on z=0,w=0 is visible when continuing in 2D");

  proj4d::BlockWorld wrongTerrain(proj4d::TerrainMode::Flat, seed, 4U);
  expect(proj4d::loadWorldSave(lowPath, wrongTerrain, error) ==
             proj4d::WorldLoadStatus::Error,
         "a terrain world cannot accidentally load another terrain's save");
  expect(proj4d::loadWorldSave(flatPath, wrongTerrain, error) ==
             proj4d::WorldLoadStatus::NotFound,
         "a terrain without a save starts as its generated world");

  const std::filesystem::path corruptPath =
      temporaryDirectory.path() / "corrupt.p4world";
  std::filesystem::copy_file(lowPath, corruptPath);
  {
    std::fstream corrupt(corruptPath,
                         std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekg(44, std::ios::beg);
    const int originalByte = corrupt.get();
    corrupt.seekp(44, std::ios::beg);
    corrupt.put(static_cast<char>(originalByte ^ 0x01));
  }
  proj4d::BlockWorld protectedWorld(proj4d::TerrainMode::Low, seed, 4U);
  expect(protectedWorld.setSolid({22, 100, 0, 0}, true),
         "the corruption test world starts with an in-memory edit");
  expect(proj4d::loadWorldSave(corruptPath, protectedWorld, error) ==
                 proj4d::WorldLoadStatus::Error &&
             error == "the world save checksum does not match" &&
             protectedWorld.isSolid({22, 100, 0, 0}),
         "a corrupt save is rejected without replacing the current world");
}

void testThreeDimensionalProjectionAndVisibleFaces() {
  proj4d::Camera3D camera;
  camera.position = {};
  const auto center = camera.project({2.0, 0.0, 0.0}, 16.0 / 9.0);
  expect(center && proj4d::nearlyEqual(center->position.x, 0.0) &&
             proj4d::nearlyEqual(center->position.y, 0.0),
         "a point ahead projects to the center of the ordinary 2D view");
  camera.turnHorizontal(0.7);
  camera.turnVertical(0.4);
  const std::array<proj4d::Vec3, 3> basis{camera.right(), camera.up(),
                                          camera.forward()};
  for (std::size_t left = 0; left < basis.size(); ++left) {
    expect(proj4d::nearlyEqual(proj4d::length(basis[left]), 1.0, 1.0e-8),
           "3D camera basis remains normalized");
    for (std::size_t right = left + 1; right < basis.size(); ++right) {
      expect(proj4d::nearlyEqual(proj4d::dot(basis[left], basis[right]), 0.0,
                                 1.0e-8),
             "3D camera basis remains orthogonal without horizon roll");
    }
  }
  camera.turnVertical(10.0);
  expect(proj4d::nearlyEqual(camera.verticalPitch(),
                             proj4d::straightVerticalPitch),
         "3D vertical mouse look stops at straight up");

  proj4d::BlockWorld backing(proj4d::TerrainMode::Flat, 4300U);
  proj4d::BlockWorld3D world(backing);
  static_cast<void>(world.setSolid({0, 100, 0}, true));
  static_cast<void>(world.setSolid({2, 100, 0}, true));
  proj4d::Camera3D xCamera;
  xCamera.position = {-2.0, 100.5, 0.5};
  const auto xSamples = proj4d::buildVisionImage3D(world, xCamera, 1, 1, 8.0);
  expect(xSamples[0].solid && xSamples[0].worldAxis == 0 &&
             xSamples[0].block == proj4d::BlockCoord3D{0, 100, 0},
         "nearest visible X face is red and hides farther cubes");
  expect(proj4d::visionFaceColors3D[0][0] > proj4d::visionFaceColors3D[0][1],
         "X-perpendicular cube faces use the red palette");
  int darkestShade = 100;
  int brightestShade = 100;
  for (int z = -4; z <= 4; ++z) {
    for (int y = -4; y <= 4; ++y) {
      for (int x = -4; x <= 4; ++x) {
        const proj4d::BlockCoord3D block{x, y, z};
        const int shade = proj4d::blockBrightnessPercent3D(block);
        darkestShade = std::min(darkestShade, shade);
        brightestShade = std::max(brightestShade, shade);
        expect(shade == proj4d::blockBrightnessPercent3D(block),
               "a 3D cube's color variation stays deterministic");
      }
    }
  }
  expect(darkestShade < 90 && brightestShade > 110,
         "3D cubes visibly include darker and brighter color variations");
  const auto shadedRed = proj4d::visionBlockColor3D(0, {7, -3, 4});
  const auto shadedGreen = proj4d::visionBlockColor3D(1, {7, -3, 4});
  const auto shadedBlue = proj4d::visionBlockColor3D(2, {7, -3, 4});
  expect(shadedRed[0] > shadedRed[1] && shadedGreen[1] > shadedGreen[0] &&
             shadedBlue[2] > shadedBlue[0],
         "per-cube shading preserves red X, green Y, and blue Z identities");
  expect(proj4d::buildCubeSelectionWireframe3D({0, 100, 0}, xCamera, 16.0 / 9.0)
                 .size() == 12U,
         "a selected cube receives all twelve white wireframe edges");
  const auto isolatedFaces = proj4d::buildVisibleFaces3D(world, {0, 100, 0}, 0);
  expect(isolatedFaces.size() == 6U,
         "an isolated 3D cube exposes exactly six faces");
  static_cast<void>(world.setSolid({1, 100, 0}, true));
  const auto joinedFaces = proj4d::buildVisibleFaces3D(world, {0, 100, 0}, 1);
  expect(std::none_of(joinedFaces.begin(), joinedFaces.end(),
                      [](const proj4d::VisibleFace3D &face) {
                        return face.block == proj4d::BlockCoord3D{0, 100, 0} &&
                               face.worldAxis == 0 && face.normalDirection == 1;
                      }),
         "a solid neighboring cube removes the blocked internal face");

  const proj4d::VisibleFace3D closeAngledFace{
      {0, 100, 0},
      0,
      -1,
      {{{0.0, 100.0, 0.0},
        {0.0, 101.0, 0.0},
        {0.0, 101.0, 1.0},
        {0.0, 100.0, 1.0}}},
  };
  proj4d::Camera3D closeCamera;
  closeCamera.position = {-0.05, 100.5, -0.05};
  closeCamera.turnHorizontal(0.78539816339744830962);
  const auto clipped =
      proj4d::projectVisibleFaces3D({closeAngledFace}, closeCamera, 16.0 / 9.0);
  expect(clipped.size() == 1U && clipped[0].vertexCount >= 3U,
         "a close wall crossing the near plane is clipped instead of erased");

  proj4d::Camera3D yCamera;
  yCamera.position = {0.5, 103.0, 0.5};
  yCamera.turnVertical(-proj4d::straightVerticalPitch);
  const auto ySamples = proj4d::buildVisionImage3D(world, yCamera, 1, 1, 8.0);
  expect(ySamples[0].solid && ySamples[0].worldAxis == 1 &&
             proj4d::visionFaceColors3D[1][1] >
                 proj4d::visionFaceColors3D[1][0],
         "Y top and bottom faces use the green palette");

  proj4d::Camera3D zCamera;
  zCamera.position = {0.5, 100.5, -2.0};
  zCamera.turnHorizontal(1.57079632679489661923);
  const auto zSamples = proj4d::buildVisionImage3D(world, zCamera, 1, 1, 8.0);
  expect(zSamples[0].solid && zSamples[0].worldAxis == 2 &&
             proj4d::visionFaceColors3D[2][2] >
                 proj4d::visionFaceColors3D[2][0],
         "Z-perpendicular cube faces use the blue palette");
}

void testThreeDimensionalPhysicsAndInteraction() {
  proj4d::BlockWorld backing(proj4d::TerrainMode::Flat, 4301U);
  proj4d::BlockWorld3D world(backing);
  proj4d::Camera3D camera;
  camera.position = {0.5, 1.0 + proj4d::playerEyeHeight, 0.5};
  const proj4d::Vec3 start = camera.position;
  proj4d::PlayerMotionState3D motion{0.0, true, start};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(camera, world, motion, {1.0, 0.0, false}, false,
                               1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(camera.position.x - start.x,
                             proj4d::playerWalkSpeed, 1.0e-8),
         "3D W movement exactly matches 4D walking speed");
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(camera, world, motion, {0.0, 1.0, false}, false,
                               1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(camera.position.z - start.z,
                             proj4d::playerWalkSpeed, 1.0e-8),
         "3D D movement is orthogonal to forward movement");

  double maximumEyeY = camera.position.y;
  proj4d::updatePlayerMotion(camera, world, motion, {}, true, 1.0 / 120.0);
  for (int step = 1; step < 240 && !motion.grounded; ++step) {
    proj4d::updatePlayerMotion(camera, world, motion, {}, false, 1.0 / 120.0);
    maximumEyeY = std::max(maximumEyeY, camera.position.y);
  }
  expect(proj4d::nearlyEqual(maximumEyeY - start.y, proj4d::playerJumpHeight,
                             0.16),
         "3D jump height and gravity exactly match the shared physics");

  proj4d::BlockWorld wallBacking(proj4d::TerrainMode::Flat, 4302U);
  proj4d::BlockWorld3D wallWorld(wallBacking);
  for (int z = -1; z <= 8; ++z) {
    static_cast<void>(wallWorld.setSolid({1, 1, z}, true));
    static_cast<void>(wallWorld.setSolid({1, 2, z}, true));
  }
  proj4d::Camera3D wallCamera;
  wallCamera.position = start;
  proj4d::PlayerMotionState3D wallMotion{0.0, true, start};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(wallCamera, wallWorld, wallMotion,
                               {1.0, 1.0, false}, false, 1.0 / 60.0);
  }
  expect(wallCamera.position.x < 0.86 && wallCamera.position.z > 6.0,
         "3D diagonal movement slides along a blocked cube face");

  proj4d::BlockWorld interactionBacking(proj4d::TerrainMode::Flat, 4303U);
  proj4d::BlockWorld3D interactionWorld(interactionBacking);
  static_cast<void>(interactionWorld.setSolid({3, 100, 0}, true));
  const proj4d::Vec3 origin{0.5, 100.5, 0.5};
  const proj4d::Vec3 forward{1.0, 0.0, 0.0};
  const auto hit = proj4d::raycast(interactionWorld, origin, forward, 8.0);
  expect(hit && hit->block == proj4d::BlockCoord3D{3, 100, 0} &&
             hit->placement == proj4d::BlockCoord3D{2, 100, 0},
         "3D ray traversal finds its cube and adjacent build position");
  const std::array<proj4d::BlockCoord3D, 0> noProtectedBlocks{};
  expect(proj4d::buildAlongRay(interactionWorld, origin, forward, 8.0,
                               noProtectedBlocks),
         "3D right click can build beside the selected cube");
  expect(proj4d::breakAlongRay(interactionWorld, origin, forward, 8.0),
         "3D left click can break the nearest selected cube");
}

void testTrueTwoDimensionalProjection() {
  expect(proj4d::visionLineWidthDivisor == 10,
         "the vertical 1D view strip is ten times thinner than it is tall");
  expect(proj4d::visionAxisColors2D[0][1] > proj4d::visionAxisColors2D[0][0] &&
             proj4d::visionAxisColors2D[1][0] >
                 proj4d::visionAxisColors2D[1][1],
         "2D colors are reversed so X is green and Y is red");
  int darkestShade = 100;
  int brightestShade = 100;
  for (int y = -4; y <= 4; ++y) {
    for (int x = -4; x <= 4; ++x) {
      const int shade = proj4d::blockBrightnessPercent2D({x, y});
      darkestShade = std::min(darkestShade, shade);
      brightestShade = std::max(brightestShade, shade);
      expect(shade == proj4d::blockBrightnessPercent2D({x, y}),
             "a 2D block's random-looking brightness stays deterministic");
    }
  }
  expect(darkestShade < 90 && brightestShade > 110,
         "2D blocks visibly include both darker and brighter variations");
  const auto shadedGreen = proj4d::visionBlockColor2D(0, {7, -3});
  const auto shadedRed = proj4d::visionBlockColor2D(1, {7, -3});
  expect(shadedGreen[1] > shadedGreen[0] && shadedRed[0] > shadedRed[1],
         "per-block shading preserves the green X and red Y identities");
  proj4d::Camera2D camera;
  camera.position = {0.5, 2.5};
  const auto center = camera.project({2.5, 2.5});
  expect(center && proj4d::nearlyEqual(center->position, 0.0),
         "a point straight ahead projects to the center of the 1D view");
  const auto above = camera.project({2.5, 3.5});
  expect(above && above->position > 0.0,
         "vertical displacement maps along the projected 1D image");
  camera.turnVertical(10.0);
  expect(proj4d::nearlyEqual(camera.verticalPitch(),
                             proj4d::straightVerticalPitch),
         "2D mouse look stops at straight up");
  camera.turnVertical(-20.0);
  expect(proj4d::nearlyEqual(camera.verticalPitch(),
                             -proj4d::straightVerticalPitch),
         "2D mouse look stops at straight down");
  camera.reverseHorizontalView();
  expect(camera.horizontalDirection() == -1 && camera.movementForward().x < 0.0,
         "Z reverses the 2D view and forward movement direction");

  proj4d::BlockWorld sideBacking(proj4d::TerrainMode::Flat, 4100U);
  proj4d::BlockWorld2D sideWorld(sideBacking);
  static_cast<void>(sideWorld.setSolid({0, 100}, true));
  static_cast<void>(sideWorld.setSolid({2, 100}, true));
  proj4d::Camera2D sideCamera;
  sideCamera.position = {-2.0, 100.5};
  const auto sideTarget = proj4d::raycast(sideWorld, sideCamera.position,
                                          sideCamera.forward(), 8.0);
  const auto sideLine = proj4d::buildVisionLine(
      sideWorld, sideCamera, 101, 8.0,
      sideTarget ? std::optional(sideTarget->block) : std::nullopt);
  expect(sideLine[50].solid && sideLine[50].worldAxis == 1,
         "an X-facing block side renders as a red Y edge interval");
  expect(sideLine[50].block == proj4d::BlockCoord2D{0, 100},
         "the nearest 2D square hides a farther square on the same sightline");
  expect(sideLine[50].targeted, "the center ray marks the targeted 2D block");
  expect(std::count_if(sideLine.begin(), sideLine.end(),
                       [](const proj4d::VisionSample2D &sample) {
                         return sample.targeted;
                       }) > 1,
         "a projected 2D square side occupies a readable rectangular interval");

  proj4d::BlockWorld floorBacking(proj4d::TerrainMode::Flat, 4101U);
  proj4d::BlockWorld2D floorWorld(floorBacking);
  static_cast<void>(floorWorld.setSolid({0, 100}, true));
  proj4d::Camera2D floorCamera;
  floorCamera.position = {0.5, 98.0};
  floorCamera.turnVertical(proj4d::straightVerticalPitch);
  const auto floorLine =
      proj4d::buildVisionLine(floorWorld, floorCamera, 101, 8.0);
  expect(floorLine[50].solid && floorLine[50].worldAxis == 0,
         "a Y-facing block side renders as a green X edge interval");
}

void testTwoDimensionalPhysicsAndInteraction() {
  proj4d::BlockWorld backingWorld(proj4d::TerrainMode::Flat, 4200U);
  proj4d::BlockWorld2D world(backingWorld);
  proj4d::Camera2D camera;
  camera.position = {0.5, 1.0 + proj4d::playerEyeHeight};
  const proj4d::Vec2 start = camera.position;
  proj4d::PlayerMotionState2D motion{0.0, true, start};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(camera, world, motion, {1.0, false}, false,
                               1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(camera.position.x - start.x,
                             proj4d::playerWalkSpeed, 1.0e-8),
         "2D walking uses the exact 4D movement speed");

  camera.reverseHorizontalView();
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(camera, world, motion, {1.0, false}, false,
                               1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(camera.position.x, start.x, 1.0e-8),
         "W follows the opposite horizontal direction after pressing Z");

  double maximumEyeY = camera.position.y;
  proj4d::updatePlayerMotion(camera, world, motion, {}, true, 1.0 / 120.0);
  for (int step = 1; step < 240 && !motion.grounded; ++step) {
    proj4d::updatePlayerMotion(camera, world, motion, {}, false, 1.0 / 120.0);
    maximumEyeY = std::max(maximumEyeY, camera.position.y);
  }
  expect(proj4d::nearlyEqual(maximumEyeY - start.y, proj4d::playerJumpHeight,
                             0.16),
         "2D jumping uses the exact 4D jump height and gravity");

  proj4d::Camera2D sneakCamera;
  sneakCamera.position = start;
  proj4d::PlayerMotionState2D sneakMotion{0.0, true, start};
  for (int frame = 0; frame < 60; ++frame) {
    proj4d::updatePlayerMotion(sneakCamera, world, sneakMotion, {1.0, true},
                               false, 1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(sneakCamera.position.x - start.x, 2.1, 1.0e-8) &&
             proj4d::nearlyEqual(sneakCamera.position.y,
                                 start.y - proj4d::playerSneakEyeDrop),
         "2D Shift sneaking matches 4D speed and eye height");

  proj4d::BlockWorld ledgeBacking(proj4d::TerrainMode::Flat, 4201U);
  proj4d::BlockWorld2D ledge(ledgeBacking);
  for (int x = 1; x <= 4; ++x) {
    static_cast<void>(ledge.setSolid({x, 0}, false));
  }
  proj4d::Camera2D edgeCamera;
  edgeCamera.position = start;
  proj4d::PlayerMotionState2D edgeMotion{0.0, true, start};
  for (int frame = 0; frame < 120; ++frame) {
    proj4d::updatePlayerMotion(edgeCamera, ledge, edgeMotion, {1.0, true},
                               false, 1.0 / 60.0);
  }
  expect(edgeCamera.position.x < 1.16 && edgeMotion.grounded,
         "2D Shift sneaking prevents walking off an X ledge");

  proj4d::BlockWorld interactionBacking(proj4d::TerrainMode::Flat, 4202U);
  proj4d::BlockWorld2D interactionWorld(interactionBacking);
  static_cast<void>(interactionWorld.setSolid({3, 100}, true));
  const proj4d::Vec2 origin{0.5, 100.5};
  const proj4d::Vec2 right{1.0, 0.0};
  const auto hit = proj4d::raycast(interactionWorld, origin, right, 8.0);
  expect(hit && hit->block == proj4d::BlockCoord2D{3, 100} &&
             hit->placement == proj4d::BlockCoord2D{2, 100},
         "2D ray traversal finds its block and build position");
  const std::array<proj4d::BlockCoord2D, 0> noProtectedBlocks{};
  expect(proj4d::buildAlongRay(interactionWorld, origin, right, 8.0,
                               noProtectedBlocks),
         "2D right click can build beside the target");
  expect(proj4d::breakAlongRay(interactionWorld, origin, right, 8.0),
         "2D left click can break the nearest target");
}

void testFlatTerrainAndPreservedDensityFunctions() {
  const proj4d::TerrainGenerator first(12345U);
  const proj4d::TerrainGenerator same(12345U);
  const proj4d::TerrainGenerator different(54321U);
  constexpr std::array<proj4d::BlockCoord, 6> samples{{
      {0, 0, 0, 0},
      {-17, 4, 23, -31},
      {1000, -50, -2000, 777},
      {-4096, 11, 3072, 2048},
      {15, 16, 17, 18},
      {-1, -2, -3, -4},
  }};

  bool seedChangesTerrain = false;
  for (const proj4d::BlockCoord &coordinate : samples) {
    expect(proj4d::nearlyEqual(first.densityAt(coordinate),
                               same.densityAt(coordinate)),
           "the preserved density function remains deterministic");
    if (!proj4d::nearlyEqual(first.densityAt(coordinate),
                             different.densityAt(coordinate), 1.0e-6)) {
      seedChangesTerrain = true;
    }
  }
  expect(seedChangesTerrain,
         "different seeds remain available to the preserved density function");
  expect(first.mode() == proj4d::TerrainMode::Flat,
         "the active terrain generator defaults to flat mode");

  const proj4d::TerrainGenerator legacyDensity(12345U,
                                               proj4d::TerrainMode::Density);
  expect(legacyDensity.mode() == proj4d::TerrainMode::Density,
         "the original density terrain remains selectable");
  for (const proj4d::BlockCoord &coordinate : samples) {
    expect(legacyDensity.generatedSolidAt(coordinate) ==
               (legacyDensity.densityAt(coordinate) > 0.0),
           "legacy density mode retains the original solidity rule");
  }
  const int densitySurface = legacyDensity.surfaceHeightAt(0, 0, 0);
  expect(legacyDensity.generatedSolidAt({0, densitySurface, 0, 0}),
         "legacy density mode retains its surface search");
  expect(!legacyDensity.generatedSolidAt({0, densitySurface + 1, 0, 0}),
         "legacy density surface search still returns the highest solid block");

  constexpr std::array<proj4d::BlockCoord, 4> horizontalSamples{{
      {0, 0, 0, 0},
      {1000000, 0, -1000000, 500000},
      {-2000000, 0, 3000000, -4000000},
      {15, 0, 16, -17},
  }};
  for (const proj4d::BlockCoord &surface : horizontalSamples) {
    expect(first.surfaceHeightAt(surface.x, surface.z, surface.w) ==
               proj4d::flatGroundSurfaceY,
           "flat terrain has the same surface across x, z, and w");
    expect(first.generatedSolidAt(surface),
           "the flat ground surface is solid everywhere");
    proj4d::BlockCoord above = surface;
    ++above.y;
    expect(!first.generatedSolidAt(above),
           "the block immediately above flat ground is air everywhere");
    proj4d::BlockCoord below = surface;
    below.y = -1000000;
    expect(first.generatedSolidAt(below),
           "flat terrain remains solid infinitely deep");
    expect(first.generatedSolidAt(surface) ==
               different.generatedSolidAt(surface),
           "active flat terrain is independent of the retained density seed");
  }

  const proj4d::Chunk surfaceChunk = first.generateChunk({7, 0, -9, 11});
  expect(surfaceChunk.isSolid({0, 0, 0, 0}),
         "surface chunks contain their y=0 ground layer");
  expect(!surfaceChunk.isSolid({0, 1, 0, 0}),
         "surface chunks contain air above the ground layer");
  const proj4d::Chunk deepChunk = first.generateChunk({-12, -5000, 34, -56});
  expect(deepChunk.isSolid({15, 15, 15, 15}),
         "arbitrarily deep chunks are completely solid");
  const proj4d::Chunk skyChunk = first.generateChunk({99, 5000, -88, 77});
  expect(!skyChunk.isSolid({0, 0, 0, 0}),
         "arbitrarily high chunks are completely air");

  proj4d::BlockWorld flatWorld(12345U);
  expect(proj4d::buildFeatureEdges(flatWorld, {0, 2, 0, 0}, 4).size() == 12U,
         "smooth flat terrain retains only a bounded outer wireframe guide");
}

void testLowTerrainMatchesHypercraftFlat() {
  const proj4d::TerrainGenerator low(2468U, proj4d::TerrainMode::Low);
  constexpr std::array<int, 9> wCoordinates{
      -64, -48, -32, -16, 0, 16, 32, 48, 64,
  };
  constexpr std::array<int, 9> hypercraftSurfaceHeights{
      16, 14, 16, 17, 18, 22, 22, 22, 19,
  };

  int minimumSurface = hypercraftSurfaceHeights.front();
  int maximumSurface = hypercraftSurfaceHeights.front();
  for (std::size_t index = 0; index < wCoordinates.size(); ++index) {
    const int w = wCoordinates[index];
    const int expectedSurface = hypercraftSurfaceHeights[index];
    const int actualSurface = low.surfaceHeightAt(8, -8, w);
    expect(actualSurface == expectedSurface,
           "Low surface heights match Hypercraft Flat's seeded 4D terrain");
    expect(low.generatedSolidAt({8, actualSurface, -8, w}),
           "Hypercraft Flat's reported Low surface is solid");
    expect(!low.generatedSolidAt({8, actualSurface + 1, -8, w}),
           "Low has air immediately above Hypercraft Flat's raw surface");
    for (int y = -128; y <= actualSurface; ++y) {
      expect(low.generatedSolidAt({8, y, -8, w}),
             "Low columns remain contiguous and cave-free below the surface");
    }
    minimumSurface = std::min(minimumSurface, actualSurface);
    maximumSurface = std::max(maximumSurface, actualSurface);
  }
  expect(maximumSurface - minimumSurface >= 4,
         "Low varies across w like Hypercraft Flat instead of forming a plane");
  expect(low.surfaceHeightAt(0, 0, 0) == 18 &&
             low.surfaceHeightAt(-31, 47, 19) == 20,
         "Low retains additional Hypercraft Flat golden surface heights");
  expect(low.generatedSolidAt({1000000, -1000000, -1000000, 500000}),
         "Low remains solid infinitely deep at distant 4D coordinates");

  const proj4d::Chunk lowSurfaceChunk = low.generateChunk({0, 1, -1, 1});
  expect(lowSurfaceChunk.isSolid({8, 6, 8, 0}),
         "Low chunks contain Hypercraft Flat's generated surface");
  expect(!lowSurfaceChunk.isSolid({8, 7, 8, 0}),
         "Low chunks contain air immediately above the generated surface");

  proj4d::BlockWorld lowWorld(proj4d::TerrainMode::Low, 2468U);
  const auto lowEdges = proj4d::buildFeatureEdges(lowWorld, {8, 22, -8, 16}, 4);
  expect(!lowEdges.empty(),
         "Low exposes visible feature edges where its generated surface bends");
  expect(lowEdges.size() != 12U,
         "Low renders generated terrain features instead of Flat's box guide");
}

void testBlockWorldUsesTheSelectedTerrainMode() {
  constexpr std::uint32_t seed = 424242U;
  proj4d::BlockWorld flat(proj4d::TerrainMode::Flat, seed, 4U);
  proj4d::BlockWorld low(proj4d::TerrainMode::Low, seed, 4U);
  proj4d::BlockWorld high(proj4d::TerrainMode::Density, seed, 4U);
  const proj4d::TerrainGenerator original(seed, proj4d::TerrainMode::Density);

  expect(flat.terrainMode() == proj4d::TerrainMode::Flat,
         "Flat menu choice creates a flat BlockWorld");
  expect(low.terrainMode() == proj4d::TerrainMode::Low,
         "Low menu choice creates a Hypercraft Flat BlockWorld");
  expect(high.terrainMode() == proj4d::TerrainMode::Density,
         "High menu choice creates a density BlockWorld");
  expect(!flat.generatedSolidAt({0, 18, 0, 0}) &&
             low.generatedSolidAt({0, 18, 0, 0}),
         "Flat and Low choices preserve their distinct terrain generators");

  bool modesDiffer = false;
  for (int w = -4; w <= 4 && !modesDiffer; w += 2) {
    for (int z = -4; z <= 4 && !modesDiffer; z += 2) {
      for (int y = -12; y <= 16 && !modesDiffer; ++y) {
        const proj4d::BlockCoord sample{2, y, z, w};
        expect(high.generatedSolidAt(sample) ==
                   original.generatedSolidAt(sample),
               "High world uses the retained original terrain function");
        modesDiffer =
            flat.generatedSolidAt(sample) != high.generatedSolidAt(sample);
      }
    }
  }
  expect(modesDiffer, "Flat and High menu choices create different terrain");
  expect(high.surfaceHeightAt(0, 0, 0) == original.surfaceHeightAt(0, 0, 0),
         "High world uses the original surface search");
}

void testInfiniteWorldCacheAndEdits() {
  proj4d::BlockWorld world(2468U, 2U);
  static_cast<void>(world.isSolid({0, 0, 0, 0}));
  static_cast<void>(world.isSolid({32, 0, 0, 0}));
  static_cast<void>(world.isSolid({64, 0, 0, 0}));
  expect(world.loadedChunkCount() <= world.maximumLoadedChunks(),
         "generated chunk cache remains bounded");

  const proj4d::BlockCoord distantEdit{1000000, 1000, -1000000, 500000};
  const bool generated = world.generatedSolidAt(distantEdit);
  expect(world.setSolid(distantEdit, !generated),
         "blocks can be edited at an arbitrarily distant 4D coordinate");
  static_cast<void>(world.isSolid({-64, 0, 0, 0}));
  static_cast<void>(world.isSolid({-96, 0, 0, 0}));
  expect(world.isSolid(distantEdit) == !generated,
         "edits survive procedural chunk eviction and regeneration");
  expect(world.loadedChunkCount() <= world.maximumLoadedChunks(),
         "regeneration still respects the cache bound");
}

void testOccludedFacesAndSmoothEdgesAreCulled() {
  proj4d::BlockWorld world(777U);
  const proj4d::BlockCoord first{15, 1000, 0, 0};
  const proj4d::BlockCoord second{16, 1000, 0, 0};
  expect(world.setSolid(first, true), "first isolated tesseract can be built");
  expect(proj4d::buildVisibleBoundaryCells(world, first, 1).size() == 8U,
         "one isolated tesseract exposes eight cubic faces");
  const auto isolatedEdges = proj4d::buildFeatureEdges(world, first, 1);
  std::array<bool, 4> representedAxes{};
  for (const proj4d::FeatureEdge4D &edge : isolatedEdges) {
    expect(edge.worldAxis >= 0 && edge.worldAxis < 4,
           "every terrain edge identifies one valid 4D direction");
    representedAxes[static_cast<std::size_t>(edge.worldAxis)] = true;
    const proj4d::Vec4 delta = edge.to - edge.from;
    for (int axis = 0; axis < 4; ++axis) {
      const double expected = axis == edge.worldAxis ? 1.0 : 0.0;
      expect(
          proj4d::nearlyEqual(delta[static_cast<std::size_t>(axis)], expected),
          "an edge's color axis matches its actual 4D direction");
    }
  }
  expect(std::ranges::all_of(representedAxes,
                             [](bool present) { return present; }),
         "an isolated tesseract exposes red X, green Y, blue Z, and purple W "
         "edge directions");
  expect(world.setSolid(second, true), "neighbor tesseract can be built");
  expect(proj4d::buildVisibleBoundaryCells(world, first, 2).size() == 14U,
         "blocked cubic faces are culled across a 4D chunk boundary");

  const std::size_t featureEdgeCount =
      proj4d::buildFeatureEdges(world, first, 2).size();
  expect(featureEdgeCount == 40U,
         "smooth face and ridge grids collapse to a clean joined outline");
}

void testTerrainOccludesUndergroundCavities() {
  proj4d::BlockWorld world(5150U);
  for (int y = 1000; y <= 1005; ++y) {
    static_cast<void>(world.setSolid({0, y, 0, 0}, false));
  }
  static_cast<void>(world.setSolid({0, 999, 0, 0}, true));

  proj4d::Camera4D camera;
  camera.position = {0.5, 1005.0, 0.0, 0.0};
  camera.turnVertical(-proj4d::straightVerticalPitch);
  const std::array<proj4d::FeatureEdge4D, 1> cavityEdge{{
      {{0.0, 1000.0, 0.0, 0.0}, {1.0, 1000.0, 0.0, 0.0}, 0},
  }};
  expect(proj4d::projectVisibleFeatureEdges(world, cavityEdge, camera).size() ==
             1U,
         "an unobstructed cavity boundary is visible");

  const std::array<proj4d::FeatureEdge4D, 1> buriedEdge{{
      {{0.0, 999.0, 0.0, 0.0}, {1.0, 999.0, 0.0, 0.0}, 0},
  }};
  expect(proj4d::projectVisibleFeatureEdges(world, buriedEdge, camera).empty(),
         "a solid block hides its own rear boundary");

  static_cast<void>(world.setSolid({0, 1002, 0, 0}, true));
  expect(proj4d::projectVisibleFeatureEdges(world, cavityEdge, camera).empty(),
         "nearer solid terrain hides an underground cavity boundary");
}

void testFourDimensionalEdgesArePartiallyOccluded() {
  const std::array<proj4d::FeatureEdge4D, 1> edge{{
      {{0.0, 100.0, 5.0, 0.0}, {1.0, 100.0, 5.0, 0.0}, 0},
  }};

  proj4d::BlockWorld midpointBlockedWorld(proj4d::TerrainMode::Flat, 6100U);
  expect(midpointBlockedWorld.setSolid({-1, 100, 4, 0}, true),
         "partial-occlusion fixture can place its midpoint blocker");
  proj4d::Camera4D midpointBlockedCamera;
  midpointBlockedCamera.position = {-2.0, 100.5, 0.5, 0.5};
  const auto midpointFull =
      proj4d::projectFeatureEdges(edge, midpointBlockedCamera);
  const auto midpointVisible = proj4d::projectVisibleFeatureEdges(
      midpointBlockedWorld, edge, midpointBlockedCamera);
  expect(midpointFull.size() == 1U && !midpointVisible.empty() &&
             totalLineLength(midpointVisible) < totalLineLength(midpointFull),
         "a blocker over an edge midpoint hides only the covered interval");

  proj4d::BlockWorld endpointBlockedWorld(proj4d::TerrainMode::Flat, 6101U);
  expect(endpointBlockedWorld.setSolid({-2, 99, 3, -1}, true),
         "partial-occlusion fixture can place its endpoint blocker");
  proj4d::Camera4D endpointBlockedCamera;
  endpointBlockedCamera.position = {-2.5, 99.25, 0.5, -1.75};
  const auto endpointFull =
      proj4d::projectFeatureEdges(edge, endpointBlockedCamera);
  const auto endpointVisible = proj4d::projectVisibleFeatureEdges(
      endpointBlockedWorld, edge, endpointBlockedCamera);
  expect(endpointFull.size() == 1U && !endpointVisible.empty() &&
             totalLineLength(endpointVisible) < totalLineLength(endpointFull),
         "a blocker away from an edge midpoint no longer lets the covered "
         "end show through");
  expect(proj4d::maximumVisibilitySamplesPerEdge <= 64,
         "partial 4D edge visibility has a fixed per-edge work bound");

  proj4d::BlockWorld selectedWorld(proj4d::TerrainMode::Flat, 6102U);
  const proj4d::BlockCoord selectedBlock{0, 100, 5, 0};
  expect(selectedWorld.setSolid(selectedBlock, true),
         "selected-wireframe fixture can place its target tesseract");
  const auto completeSelection =
      proj4d::buildTesseractWireframe(selectedBlock, midpointBlockedCamera);
  const auto visibleSelection = proj4d::buildVisibleTesseractWireframe(
      selectedWorld, selectedBlock, midpointBlockedCamera);
  expect(!visibleSelection.empty() && totalLineLength(visibleSelection) <
                                          totalLineLength(completeSelection),
         "the white selection wireframe is occluded by its solid tesseract");

  proj4d::Camera4D clippingCamera;
  clippingCamera.position = {};
  const std::array<proj4d::FeatureEdge4D, 1> nearCrossing{{
      {{0.1, 0.0, 0.01, 0.0}, {0.1, 0.0, 1.0, 0.0}, 2},
  }};
  const std::array<proj4d::FeatureEdge4D, 1> farCrossing{{
      {{0.1, 0.0, 23.5, 0.0}, {0.1, 0.0, 24.5, 0.0}, 2},
  }};
  expect(proj4d::projectFeatureEdges(nearCrossing, clippingCamera).size() == 1U,
         "an edge crossing the 4D camera near plane is clipped, not erased");
  expect(proj4d::projectFeatureEdges(farCrossing, clippingCamera).size() == 1U,
         "an edge crossing the 4D camera far plane is clipped, not erased");
}

void testRaycastBuildingAndBreaking() {
  proj4d::BlockWorld world(9090U);
  const proj4d::BlockCoord target{0, 1000, 0, 0};
  expect(world.setSolid(target, true), "raycast fixture can be built");
  const proj4d::Vec4 origin{0.5, 1002.5, 0.5, 0.5};
  const proj4d::Vec4 downward{0.0, -1.0, 0.0, 0.0};
  const auto hit = proj4d::raycast(world, origin, downward, 5.0);
  expect(hit.has_value(), "4D grid traversal hits a generated chunk block");
  expect(hit && hit->block == target, "ray identifies the nearest block");
  expect(hit && hit->placement == proj4d::BlockCoord{0, 1001, 0, 0},
         "ray identifies the adjacent build coordinate");

  const std::array<proj4d::BlockCoord, 1> protectedBlock{{
      {0, 1001, 0, 0},
  }};
  expect(!proj4d::buildAlongRay(world, origin, downward, 5.0, protectedBlock),
         "building cannot overlap a protected player coordinate");
  const std::array<proj4d::BlockCoord, 0> noProtectedBlocks{};
  expect(proj4d::buildAlongRay(world, origin, downward, 5.0, noProtectedBlocks),
         "right click builds beside the targeted tesseract");
  expect(proj4d::breakAlongRay(world, origin, downward, 5.0),
         "left click breaks the nearest targeted tesseract");
}

void testVisionGeometryIsBounded() {
  proj4d::BlockWorld world(13579U);
  const int surface = world.surfaceHeightAt(0, 0, 0);
  proj4d::Camera4D camera;
  camera.position = {0.5, static_cast<double>(surface) + 2.1, 0.5, 0.5};
  camera.turnVertical(-0.28);
  const proj4d::BlockCoord center = proj4d::containingBlock(camera.position);
  const auto lines = proj4d::buildVisionGeometry(world, camera, center, 4);
  expect(!lines.empty(), "flat 4D terrain produces bounded wireframe geometry");
  for (const auto &line : lines) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      expect(line.from[axis] >= -1.000001 && line.from[axis] <= 1.000001 &&
                 line.to[axis] >= -1.000001 && line.to[axis] <= 1.000001,
             "projected geometry is clipped to the solid vision cube");
    }
  }
  constexpr int diameter = proj4d::renderBlockRadius * 2 + 1;
  expect(diameter * diameter * diameter * diameter < 30000,
         "render extraction has a fixed bounded 4D work region");
}

} // namespace

int main() {
  try {
    testCameraProjectionAndRotation();
    testVerticalLookStopsAtStraightUpAndDown();
    testHorizontalLookKeepsTheViewLevel();
    testMouseMovementTurnsInOrdinaryAndFourthDimensions();
    testCameraProvidesTwoOrthogonalSidewaysDirections();
    testViewStatusReportsFourCoordinatesAndThreeAngles();
    testGroundedPlayerCanMoveWithoutJumping();
    testPlayerPhysicsMatchesHypercraft();
    testPlayerSneaksLikeMinecraft();
    testPlayerSlidesAlongBlockedFaces();
    testPlayerJumpsOneAndAHalfBlocks();
    testTrueFourDimensionalChunks();
    testTrueThreeDimensionalSharedSlice();
    testTrueTwoDimensionalWorldAndChunks();
    testSharedWorldSaving();
    testThreeDimensionalProjectionAndVisibleFaces();
    testThreeDimensionalPhysicsAndInteraction();
    testTrueTwoDimensionalProjection();
    testTwoDimensionalPhysicsAndInteraction();
    testFlatTerrainAndPreservedDensityFunctions();
    testLowTerrainMatchesHypercraftFlat();
    testBlockWorldUsesTheSelectedTerrainMode();
    testInfiniteWorldCacheAndEdits();
    testOccludedFacesAndSmoothEdgesAreCulled();
    testTerrainOccludesUndergroundCavities();
    testFourDimensionalEdgesArePartiallyOccluded();
    testRaycastBuildingAndBreaking();
    testVisionGeometryIsBounded();
  } catch (const std::exception &error) {
    std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All Proj4D tests passed\n";
  return EXIT_SUCCESS;
}
