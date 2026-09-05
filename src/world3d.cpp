#include "proj4d/world3d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace proj4d {

BlockCoord blockCoord4D(const BlockCoord3D &coordinate) {
  return {coordinate.x, coordinate.y, coordinate.z, 0};
}

BlockWorld3D::BlockWorld3D(BlockWorld &world) : world_(world) {}

bool BlockWorld3D::isSolid(const BlockCoord3D &coordinate) const {
  return world_.isSolid(blockCoord4D(coordinate));
}

bool BlockWorld3D::generatedSolidAt(const BlockCoord3D &coordinate) const {
  return world_.generatedSolidAt(blockCoord4D(coordinate));
}

bool BlockWorld3D::setSolid(const BlockCoord3D &coordinate, bool solid) {
  return world_.setSolid(blockCoord4D(coordinate), solid);
}

int BlockWorld3D::surfaceHeightAt(int x, int z) const {
  return world_.surfaceHeightAt(x, z, 0);
}

std::uint32_t BlockWorld3D::seed() const { return world_.seed(); }

TerrainMode BlockWorld3D::terrainMode() const { return world_.terrainMode(); }

std::size_t BlockWorld3D::loadedChunkCount() const {
  return world_.loadedChunkCount();
}

std::size_t BlockWorld3D::maximumLoadedChunks() const {
  return world_.maximumLoadedChunks();
}

std::uint64_t BlockWorld3D::revision() const { return world_.revision(); }

BlockCoord3D containingBlock(const Vec3 &point) {
  return {static_cast<int>(std::floor(point.x)),
          static_cast<int>(std::floor(point.y)),
          static_cast<int>(std::floor(point.z))};
}

std::optional<RayHit3D> raycast(const BlockWorld3D &world, const Vec3 &origin,
                                const Vec3 &direction, double maximumDistance) {
  const Vec3 rayDirection = normalized(direction);
  BlockCoord3D current = containingBlock(origin);
  BlockCoord3D previous = current;
  std::array<int, 3> step{};
  std::array<double, 3> nextBoundary{};
  std::array<double, 3> boundaryStride{};

  for (std::size_t axis = 0; axis < 3; ++axis) {
    const double component = rayDirection[axis];
    if (std::abs(component) <= 1.0e-12) {
      nextBoundary[axis] = std::numeric_limits<double>::infinity();
      boundaryStride[axis] = std::numeric_limits<double>::infinity();
      continue;
    }
    step[axis] = component > 0.0 ? 1 : -1;
    const double boundary = component > 0.0
                                ? static_cast<double>(current[axis] + 1)
                                : static_cast<double>(current[axis]);
    nextBoundary[axis] = (boundary - origin[axis]) / component;
    boundaryStride[axis] = std::abs(1.0 / component);
  }

  double distance = 0.0;
  int enteredAxis = -1;
  while (distance <= maximumDistance) {
    if (world.isSolid(current)) {
      return RayHit3D{current, previous, distance, enteredAxis};
    }
    std::size_t selectedAxis = 0;
    for (std::size_t axis = 1; axis < 3; ++axis) {
      if (nextBoundary[axis] < nextBoundary[selectedAxis]) {
        selectedAxis = axis;
      }
    }
    distance = nextBoundary[selectedAxis];
    if (!std::isfinite(distance) || distance > maximumDistance) {
      break;
    }
    previous = current;
    current[selectedAxis] += step[selectedAxis];
    nextBoundary[selectedAxis] += boundaryStride[selectedAxis];
    enteredAxis = static_cast<int>(selectedAxis);
  }
  return std::nullopt;
}

bool breakAlongRay(BlockWorld3D &world, const Vec3 &origin,
                   const Vec3 &direction, double maximumDistance) {
  const auto hit = raycast(world, origin, direction, maximumDistance);
  return hit && world.setSolid(hit->block, false);
}

bool buildAlongRay(BlockWorld3D &world, const Vec3 &origin,
                   const Vec3 &direction, double maximumDistance,
                   std::span<const BlockCoord3D> protectedBlocks) {
  const auto hit = raycast(world, origin, direction, maximumDistance);
  if (!hit || world.isSolid(hit->placement) ||
      std::find(protectedBlocks.begin(), protectedBlocks.end(),
                hit->placement) != protectedBlocks.end()) {
    return false;
  }
  return world.setSolid(hit->placement, true);
}

} // namespace proj4d
