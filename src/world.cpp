#include "proj4d/world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace proj4d {

namespace {

BlockCoord offset(BlockCoord coordinate, int axis, int amount) {
  coordinate[static_cast<std::size_t>(axis)] += amount;
  return coordinate;
}

} // namespace

BlockWorld::BlockWorld(WorldBounds bounds) : bounds_(bounds) {
  std::size_t blockCount = 1;
  for (std::size_t axis = 0; axis < 4; ++axis) {
    const int extent = bounds_.maximum[axis] - bounds_.minimum[axis] + 1;
    if (extent <= 0) {
      throw std::invalid_argument("world bounds must have positive extents");
    }
    extents_[axis] = extent;
    blockCount *= static_cast<std::size_t>(extent);
  }
  blocks_.assign(blockCount, false);
}

void BlockWorld::fillFlatGround() {
  for (int w = bounds_.minimum.w; w <= bounds_.maximum.w; ++w) {
    for (int z = bounds_.minimum.z; z <= bounds_.maximum.z; ++z) {
      for (int x = bounds_.minimum.x; x <= bounds_.maximum.x; ++x) {
        static_cast<void>(setSolid({x, 0, z, w}, true));
      }
    }
  }
}

bool BlockWorld::inBounds(const BlockCoord &coordinate) const {
  for (std::size_t axis = 0; axis < 4; ++axis) {
    if (coordinate[axis] < bounds_.minimum[axis] ||
        coordinate[axis] > bounds_.maximum[axis]) {
      return false;
    }
  }
  return true;
}

std::size_t BlockWorld::indexOf(const BlockCoord &coordinate) const {
  if (!inBounds(coordinate)) {
    throw std::out_of_range("4D block coordinate is outside the world");
  }
  std::size_t index = 0;
  std::size_t stride = 1;
  for (std::size_t axis = 0; axis < 4; ++axis) {
    index +=
        static_cast<std::size_t>(coordinate[axis] - bounds_.minimum[axis]) *
        stride;
    stride *= static_cast<std::size_t>(extents_[axis]);
  }
  return index;
}

bool BlockWorld::isSolid(const BlockCoord &coordinate) const {
  return inBounds(coordinate) && blocks_[indexOf(coordinate)];
}

bool BlockWorld::setSolid(const BlockCoord &coordinate, bool solid) {
  if (!inBounds(coordinate)) {
    return false;
  }
  blocks_[indexOf(coordinate)] = solid;
  return true;
}

std::size_t BlockWorld::solidCount() const {
  return static_cast<std::size_t>(
      std::count(blocks_.begin(), blocks_.end(), true));
}

const WorldBounds &BlockWorld::bounds() const { return bounds_; }

std::vector<BoundaryCell> BlockWorld::exposedBoundaryCells() const {
  std::vector<BoundaryCell> cells;
  cells.reserve(solidCount() * 4U);
  for (int w = bounds_.minimum.w; w <= bounds_.maximum.w; ++w) {
    for (int z = bounds_.minimum.z; z <= bounds_.maximum.z; ++z) {
      for (int y = bounds_.minimum.y; y <= bounds_.maximum.y; ++y) {
        for (int x = bounds_.minimum.x; x <= bounds_.maximum.x; ++x) {
          const BlockCoord block{x, y, z, w};
          if (!isSolid(block)) {
            continue;
          }
          for (int axis = 0; axis < 4; ++axis) {
            for (const int side : {-1, 1}) {
              if (!isSolid(offset(block, axis, side))) {
                cells.push_back({block, axis, side});
              }
            }
          }
        }
      }
    }
  }
  return cells;
}

BlockCoord containingBlock(const Vec4 &point) {
  return {
      static_cast<int>(std::floor(point.x)),
      static_cast<int>(std::floor(point.y)),
      static_cast<int>(std::floor(point.z)),
      static_cast<int>(std::floor(point.w)),
  };
}

std::optional<RayHit> raycast(const BlockWorld &world, const Vec4 &origin,
                              const Vec4 &direction, double maximumDistance) {
  const Vec4 rayDirection = normalized(direction);
  std::optional<RayHit> closest;
  const WorldBounds &bounds = world.bounds();

  for (int w = bounds.minimum.w; w <= bounds.maximum.w; ++w) {
    for (int z = bounds.minimum.z; z <= bounds.maximum.z; ++z) {
      for (int y = bounds.minimum.y; y <= bounds.maximum.y; ++y) {
        for (int x = bounds.minimum.x; x <= bounds.maximum.x; ++x) {
          const BlockCoord block{x, y, z, w};
          if (!world.isSolid(block)) {
            continue;
          }
          double nearDistance = 0.0;
          double farDistance = maximumDistance;
          int entryAxis = -1;
          int entrySide = 0;
          bool intersects = true;

          for (int axis = 0; axis < 4; ++axis) {
            const auto axisIndex = static_cast<std::size_t>(axis);
            const double originValue = origin[axisIndex];
            const double directionValue = rayDirection[axisIndex];
            const double minimum = static_cast<double>(block[axisIndex]);
            const double maximum = minimum + 1.0;
            if (std::abs(directionValue) <= 1.0e-12) {
              if (originValue < minimum || originValue > maximum) {
                intersects = false;
                break;
              }
              continue;
            }
            double first = (minimum - originValue) / directionValue;
            double second = (maximum - originValue) / directionValue;
            int side = -1;
            if (first > second) {
              std::swap(first, second);
              side = 1;
            }
            if (first > nearDistance) {
              nearDistance = first;
              entryAxis = axis;
              entrySide = side;
            }
            farDistance = std::min(farDistance, second);
            if (nearDistance > farDistance) {
              intersects = false;
              break;
            }
          }

          if (!intersects || nearDistance < 0.0 ||
              nearDistance > maximumDistance ||
              (closest && nearDistance >= closest->distance)) {
            continue;
          }
          BlockCoord placement = block;
          if (entryAxis >= 0) {
            placement[static_cast<std::size_t>(entryAxis)] += entrySide;
          }
          closest = RayHit{block, placement, nearDistance};
        }
      }
    }
  }
  return closest;
}

bool breakAlongRay(BlockWorld &world, const Vec4 &origin, const Vec4 &direction,
                   double maximumDistance) {
  const auto hit = raycast(world, origin, direction, maximumDistance);
  return hit && world.setSolid(hit->block, false);
}

bool buildAlongRay(BlockWorld &world, const Vec4 &origin, const Vec4 &direction,
                   double maximumDistance,
                   std::span<const BlockCoord> protectedBlocks) {
  const auto hit = raycast(world, origin, direction, maximumDistance);
  if (!hit || !world.inBounds(hit->placement) ||
      world.isSolid(hit->placement) ||
      std::find(protectedBlocks.begin(), protectedBlocks.end(),
                hit->placement) != protectedBlocks.end()) {
    return false;
  }
  return world.setSolid(hit->placement, true);
}

} // namespace proj4d
