#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "proj4d/camera.hpp"
#include "proj4d/render_geometry.hpp"
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
  const std::array<proj4d::Vec4, 4> basis{camera.imageX, camera.imageY,
                                          camera.imageZ, camera.forward};
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

void testWorldAndSharedBoundaryCulling() {
  proj4d::BlockWorld world({{0, 0, 0, 0}, {1, 0, 0, 0}});
  expect(world.setSolid({0, 0, 0, 0}, true), "first block can be built");
  expect(world.exposedBoundaryCells().size() == 8U,
         "one tesseract has eight cubic cells");
  expect(world.setSolid({1, 0, 0, 0}, true), "second block can be built");
  expect(world.exposedBoundaryCells().size() == 14U,
         "shared cubic boundary is culled in both neighboring tesseracts");
  const std::size_t featureEdgeCount = proj4d::buildFeatureEdges(world).size();
  expect(featureEdgeCount == 40U, "smooth face and ridge grids collapse to a "
                                  "clean hyperrectangle outline (got " +
                                      std::to_string(featureEdgeCount) + ")");
  expect(world.solidCount() == 2U, "world tracks solid tesseracts");
  expect(!world.setSolid({2, 0, 0, 0}, true),
         "world rejects out-of-bounds builds");
}

void testFlatFieldAndRaycast() {
  proj4d::BlockWorld world({{-1, -1, -1, -1}, {1, 2, 1, 1}});
  world.fillFlatGround();
  expect(world.solidCount() == 27U, "flat field spans x, z, and w");
  const auto hit =
      proj4d::raycast(world, {0.5, 1.5, 0.5, 0.5}, {0.0, -1.0, 0.0, 0.0}, 4.0);
  expect(hit.has_value(), "4D ray hits flat field");
  expect(hit && hit->block == proj4d::BlockCoord{0, 0, 0, 0},
         "ray identifies block");
  expect(hit && hit->placement == proj4d::BlockCoord{0, 1, 0, 0},
         "ray identifies adjacent build coordinate");

  const std::array<proj4d::BlockCoord, 1> noBuildAt{{{0, 1, 0, 0}}};
  expect(!proj4d::buildAlongRay(world, {0.5, 1.5, 0.5, 0.5},
                                {0.0, -1.0, 0.0, 0.0}, 4.0, noBuildAt),
         "building cannot overlap a protected player coordinate");
  const std::array<proj4d::BlockCoord, 0> noProtectedBlocks{};
  expect(proj4d::buildAlongRay(world, {0.5, 1.5, 0.5, 0.5},
                               {0.0, -1.0, 0.0, 0.0}, 4.0, noProtectedBlocks),
         "right-click behavior builds beside the hit tesseract");
  expect(world.isSolid({0, 1, 0, 0}), "built tesseract becomes solid");
  expect(proj4d::breakAlongRay(world, {0.5, 2.5, 0.5, 0.5},
                               {0.0, -1.0, 0.0, 0.0}, 4.0),
         "left-click behavior breaks the nearest tesseract");
  expect(!world.isSolid({0, 1, 0, 0}), "broken tesseract becomes empty");
}

void testVisionGeometryIsBounded() {
  proj4d::BlockWorld world({{-1, -1, -1, -1}, {1, 1, 2, 1}});
  world.fillFlatGround();
  proj4d::Camera4D camera;
  camera.position = {0.5, 2.1, -2.0, 0.5};
  const auto lines = proj4d::buildVisionGeometry(world, camera);
  expect(!lines.empty(), "4D world produces projected wireframe geometry");
  for (const auto &line : lines) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      expect(line.from[axis] >= -1.000001 && line.from[axis] <= 1.000001 &&
                 line.to[axis] >= -1.000001 && line.to[axis] <= 1.000001,
             "projected geometry is clipped to solid vision cube");
    }
  }
}

} // namespace

int main() {
  try {
    testCameraProjectionAndRotation();
    testWorldAndSharedBoundaryCulling();
    testFlatFieldAndRaycast();
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
