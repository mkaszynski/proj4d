#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "proj4d/camera.hpp"
#include "proj4d/chunk.hpp"
#include "proj4d/mouse_input.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/render_geometry.hpp"
#include "proj4d/terrain_generator.hpp"
#include "proj4d/view_status.hpp"
#include "proj4d/world.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
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
  const proj4d::MouseMotionMapping worldMotion =
      proj4d::mapMouseMotion(25.0, -50.0, false);
  proj4d::Camera4D camera;
  camera.turnHorizontal(worldMotion.worldHorizontalTurn);
  camera.turnFourth(worldMotion.worldFourthTurn);

  expect(proj4d::nearlyEqual(camera.horizontalAngle(), 0.1, 1.0e-8),
         "rightward mouse movement turns in the same direction as D");
  expect(proj4d::nearlyEqual(camera.fourthAngle(), -0.2, 1.0e-8),
         "upward mouse movement turns in the same direction as Q");
  expect(proj4d::nearlyEqual(camera.verticalPitch(), 0.0),
         "ordinary mouse look does not change vertical pitch");
  expect(proj4d::nearlyEqual(worldMotion.visionCubeYawTurn, 0.0) &&
             proj4d::nearlyEqual(worldMotion.visionCubePitchTurn, 0.0),
         "unmodified mouse movement does not orbit the vision cube");

  const proj4d::MouseMotionMapping controlMotion =
      proj4d::mapMouseMotion(25.0, -50.0, true);
  expect(proj4d::nearlyEqual(controlMotion.worldHorizontalTurn, 0.0) &&
             proj4d::nearlyEqual(controlMotion.worldFourthTurn, 0.0),
         "Ctrl plus mouse leaves the 4D world look unchanged");
  expect(
      proj4d::nearlyEqual(controlMotion.visionCubeYawTurn, 0.1, 1.0e-8) &&
          proj4d::nearlyEqual(controlMotion.visionCubePitchTurn, -0.2, 1.0e-8),
      "Ctrl plus mouse retains the original vision-cube orbit");
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

  proj4d::updatePlayerMotion(camera, world, motion, 1.0, false, 0.1);
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
    proj4d::updatePlayerMotion(camera, flatWorld, motion, 1.0, false,
                               1.0 / 60.0);
  }
  expect(proj4d::nearlyEqual(camera.position.z - start.z, 7.0, 1.0e-8),
         "one second of forward input travels seven blocks like Hypercraft");

  proj4d::Camera4D clampedCamera;
  clampedCamera.position = start;
  proj4d::PlayerMotionState clampedMotion{0.0, true, start};
  proj4d::updatePlayerMotion(clampedCamera, flatWorld, clampedMotion, 1.0,
                             false, 1.0);
  expect(proj4d::nearlyEqual(clampedCamera.position.z - start.z, 0.35, 1.0e-8),
         "a long frame uses Hypercraft's 50 millisecond physics cap");

  proj4d::BlockWorld collisionWorld(proj4d::TerrainMode::Flat, 2027U);
  static_cast<void>(collisionWorld.setSolid({0, 100, 0, 1}, true));
  expect(!proj4d::playerCollidesAt(collisionWorld, {0.5, 100.5, 0.5, 0.84}),
         "the narrow 4D body can approach a neighboring w block");
  expect(proj4d::playerCollidesAt(collisionWorld, {0.5, 100.5, 0.5, 0.86}),
         "the 4D body collides after its radius reaches a neighboring w block");
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

  proj4d::updatePlayerMotion(camera, world, motion, 1.0, false, 0.2);
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
  proj4d::updatePlayerMotion(camera, world, motion, 0.0, true, 1.0 / 120.0);
  expect(proj4d::nearlyEqual(
             motion.verticalVelocity,
             std::sqrt(2.0 * proj4d::playerGravity * proj4d::playerJumpHeight) -
                 proj4d::playerGravity / 120.0,
             1.0e-8),
         "jump launch velocity and first gravity step match Hypercraft");
  maximumEyeY = std::max(maximumEyeY, camera.position.y);
  for (int step = 1; step < 240 && !motion.grounded; ++step) {
    proj4d::updatePlayerMotion(camera, world, motion, 0.0, false, 1.0 / 120.0);
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

void testBlockWorldUsesTheSelectedTerrainMode() {
  constexpr std::uint32_t seed = 424242U;
  proj4d::BlockWorld flat(proj4d::TerrainMode::Flat, seed, 4U);
  proj4d::BlockWorld normal(proj4d::TerrainMode::Density, seed, 4U);
  const proj4d::TerrainGenerator original(seed, proj4d::TerrainMode::Density);

  expect(flat.terrainMode() == proj4d::TerrainMode::Flat,
         "Flat menu choice creates a flat BlockWorld");
  expect(normal.terrainMode() == proj4d::TerrainMode::Density,
         "Normal menu choice creates a density BlockWorld");

  bool modesDiffer = false;
  for (int w = -4; w <= 4 && !modesDiffer; w += 2) {
    for (int z = -4; z <= 4 && !modesDiffer; z += 2) {
      for (int y = -12; y <= 16 && !modesDiffer; ++y) {
        const proj4d::BlockCoord sample{2, y, z, w};
        expect(normal.generatedSolidAt(sample) ==
                   original.generatedSolidAt(sample),
               "Normal world uses the retained original terrain function");
        modesDiffer =
            flat.generatedSolidAt(sample) != normal.generatedSolidAt(sample);
      }
    }
  }
  expect(modesDiffer, "Flat and Normal menu choices create different terrain");
  expect(normal.surfaceHeightAt(0, 0, 0) == original.surfaceHeightAt(0, 0, 0),
         "Normal world uses the original surface search");
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
      {{0.0, 1000.0, 0.0, 0.0}, {1.0, 1000.0, 0.0, 0.0}, 1},
  }};
  expect(proj4d::projectVisibleFeatureEdges(world, cavityEdge, camera).size() ==
             1U,
         "an unobstructed cavity boundary is visible");

  const std::array<proj4d::FeatureEdge4D, 1> buriedEdge{{
      {{0.0, 999.0, 0.0, 0.0}, {1.0, 999.0, 0.0, 0.0}, 1},
  }};
  expect(proj4d::projectVisibleFeatureEdges(world, buriedEdge, camera).empty(),
         "a solid block hides its own rear boundary");

  static_cast<void>(world.setSolid({0, 1002, 0, 0}, true));
  expect(proj4d::projectVisibleFeatureEdges(world, cavityEdge, camera).empty(),
         "nearer solid terrain hides an underground cavity boundary");
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
    testViewStatusReportsFourCoordinatesAndThreeAngles();
    testGroundedPlayerCanMoveWithoutJumping();
    testPlayerPhysicsMatchesHypercraft();
    testPlayerSlidesAlongBlockedFaces();
    testPlayerJumpsOneAndAHalfBlocks();
    testTrueFourDimensionalChunks();
    testFlatTerrainAndPreservedDensityFunctions();
    testBlockWorldUsesTheSelectedTerrainMode();
    testInfiniteWorldCacheAndEdits();
    testOccludedFacesAndSmoothEdgesAreCulled();
    testTerrainOccludesUndergroundCavities();
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
