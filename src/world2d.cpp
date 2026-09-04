#include "proj4d/world2d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace proj4d {

BlockCoord blockCoord4D(const BlockCoord2D &coordinate) {
  return {coordinate.x, coordinate.y, 0, 0};
}

BlockWorld2D::BlockWorld2D(BlockWorld &world) : world_(world) {}

bool BlockWorld2D::isSolid(const BlockCoord2D &coordinate) const {
  return world_.isSolid(blockCoord4D(coordinate));
}

bool BlockWorld2D::generatedSolidAt(const BlockCoord2D &coordinate) const {
  return world_.generatedSolidAt(blockCoord4D(coordinate));
}

bool BlockWorld2D::setSolid(const BlockCoord2D &coordinate, bool solid) {
  return world_.setSolid(blockCoord4D(coordinate), solid);
}

int BlockWorld2D::surfaceHeightAt(int x) const {
  return world_.surfaceHeightAt(x, 0, 0);
}

std::uint32_t BlockWorld2D::seed() const { return world_.seed(); }

TerrainMode BlockWorld2D::terrainMode() const { return world_.terrainMode(); }

std::size_t BlockWorld2D::loadedChunkCount() const {
  return world_.loadedChunkCount();
}

std::size_t BlockWorld2D::maximumLoadedChunks() const {
  return world_.maximumLoadedChunks();
}

std::uint64_t BlockWorld2D::revision() const { return world_.revision(); }

BlockCoord2D containingBlock(const Vec2 &point) {
  return {static_cast<int>(std::floor(point.x)),
          static_cast<int>(std::floor(point.y))};
}

std::optional<RayHit2D> raycast(const BlockWorld2D &world, const Vec2 &origin,
                                const Vec2 &direction, double maximumDistance) {
  const Vec2 rayDirection = normalized(direction);
  BlockCoord2D current = containingBlock(origin);
  BlockCoord2D previous = current;
  std::array<int, 2> step{};
  std::array<double, 2> nextBoundary{};
  std::array<double, 2> boundaryStride{};

  for (std::size_t axis = 0; axis < 2; ++axis) {
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
      return RayHit2D{current, previous, distance, enteredAxis};
    }
    const std::size_t selectedAxis =
        nextBoundary[1] < nextBoundary[0] ? 1U : 0U;
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

bool breakAlongRay(BlockWorld2D &world, const Vec2 &origin,
                   const Vec2 &direction, double maximumDistance) {
  const auto hit = raycast(world, origin, direction, maximumDistance);
  return hit && world.setSolid(hit->block, false);
}

bool buildAlongRay(BlockWorld2D &world, const Vec2 &origin,
                   const Vec2 &direction, double maximumDistance,
                   std::span<const BlockCoord2D> protectedBlocks) {
  const auto hit = raycast(world, origin, direction, maximumDistance);
  if (!hit || world.isSolid(hit->placement) ||
      std::find(protectedBlocks.begin(), protectedBlocks.end(),
                hit->placement) != protectedBlocks.end()) {
    return false;
  }
  return world.setSolid(hit->placement, true);
}

} // namespace proj4d
