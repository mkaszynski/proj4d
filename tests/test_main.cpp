#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "proj4d/camera.hpp"
#include "proj4d/chunk.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/render_geometry.hpp"
#include "proj4d/terrain_generator.hpp"
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

  proj4d::updatePlayerMotion(camera, world, motion, 1.0, 0.1);
  expect(camera.position.z > spawnPosition.z,
         "grounded player moves forward without jumping");
  expect(motion.grounded, "player remains grounded after horizontal movement");
  expect(proj4d::nearlyEqual(camera.position.y, spawnPosition.y),
         "ground contact preserves standing eye height");
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

  proj4d::updatePlayerMotion(camera, world, motion, 1.0, 0.2);
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
  proj4d::requestPlayerJump(motion);

  double maximumEyeY = camera.position.y;
  for (int step = 0; step < 240 && !motion.grounded; ++step) {
    proj4d::updatePlayerMotion(camera, world, motion, 0.0, 1.0 / 120.0);
    maximumEyeY = std::max(maximumEyeY, camera.position.y);
  }
  expect(proj4d::nearlyEqual(maximumEyeY - spawnPosition.y,
                             proj4d::playerJumpHeight, 1.0e-3),
         "jump rises one and a half blocks");
  expect(motion.grounded, "jump finishes by landing on the ground");
  expect(proj4d::nearlyEqual(camera.position.y, spawnPosition.y),
         "landing restores the standing eye height");
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

void testProceduralDensityField() {
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
           "the same seed reproduces identical 4D density");
    if (!proj4d::nearlyEqual(first.densityAt(coordinate),
                             different.densityAt(coordinate), 1.0e-6)) {
      seedChangesTerrain = true;
    }
  }
  expect(seedChangesTerrain, "different seeds produce different terrain");
  expect(first.generatedSolidAt({1000000, -1000000, -1000000, 500000}),
         "terrain remains solid at arbitrarily deep negative y");

  const int surface = first.surfaceHeightAt(0, 0, 0);
  expect(first.generatedSolidAt({0, surface, 0, 0}),
         "surface search returns a solid block");
  expect(!first.generatedSolidAt({0, surface + 1, 0, 0}),
         "surface search returns the highest solid block");

  bool foundMultipleTransitions = false;
  for (int x = -8; x <= 8 && !foundMultipleTransitions; x += 2) {
    for (int z = -8; z <= 8 && !foundMultipleTransitions; z += 2) {
      for (int w = -8; w <= 8 && !foundMultipleTransitions; w += 2) {
        int transitions = 0;
        bool previous = first.generatedSolidAt({x, -20, z, w});
        for (int y = -19; y <= 28; ++y) {
          const bool current = first.generatedSolidAt({x, y, z, w});
          transitions += current != previous ? 1 : 0;
          previous = current;
        }
        foundMultipleTransitions = transitions >= 3;
      }
    }
  }
  expect(foundMultipleTransitions, "4D density terrain supports multiple "
                                   "solid/air transitions on one y line");
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
  expect(!lines.empty(), "procedural 4D terrain produces wireframe geometry");
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
    testGroundedPlayerCanMoveWithoutJumping();
    testPlayerSlidesAlongBlockedFaces();
    testPlayerJumpsOneAndAHalfBlocks();
    testTrueFourDimensionalChunks();
    testProceduralDensityField();
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
