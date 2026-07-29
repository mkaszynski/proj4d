#include "proj4d/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace proj4d {

BlockWorld::BlockWorld(std::uint32_t seed, std::size_t maximumLoadedChunks)
    : generator_(seed),
      maximumLoadedChunks_(std::max<std::size_t>(1U, maximumLoadedChunks)) {}

Chunk &BlockWorld::ensureChunk(const ChunkCoord &coordinate) const {
  if (auto existing = chunks_.find(coordinate); existing != chunks_.end()) {
    existing->second.lastAccess = ++accessClock_;
    return existing->second.chunk;
  }

  Chunk generated = generator_.generateChunk(coordinate);
  for (const auto &[block, solid] : overrides_) {
    if (chunkCoordForBlock(block) == coordinate) {
      generated.setSolid(localCoordForBlock(block), solid);
    }
  }
  auto [inserted, wasInserted] = chunks_.try_emplace(
      coordinate, CachedChunk{std::move(generated), ++accessClock_});
  static_cast<void>(wasInserted);
  evictLeastRecentlyUsed(coordinate);
  return inserted->second.chunk;
}

void BlockWorld::evictLeastRecentlyUsed(
    const ChunkCoord &protectedChunk) const {
  while (chunks_.size() > maximumLoadedChunks_) {
    auto candidate = chunks_.end();
    for (auto current = chunks_.begin(); current != chunks_.end(); ++current) {
      if (current->first == protectedChunk) {
        continue;
      }
      if (candidate == chunks_.end() ||
          current->second.lastAccess < candidate->second.lastAccess) {
        candidate = current;
      }
    }
    if (candidate == chunks_.end()) {
      return;
    }
    chunks_.erase(candidate);
  }
}

bool BlockWorld::isSolid(const BlockCoord &coordinate) const {
  Chunk &chunk = ensureChunk(chunkCoordForBlock(coordinate));
  return chunk.isSolid(localCoordForBlock(coordinate));
}

bool BlockWorld::generatedSolidAt(const BlockCoord &coordinate) const {
  return generator_.generatedSolidAt(coordinate);
}

bool BlockWorld::setSolid(const BlockCoord &coordinate, bool solid) {
  if (isSolid(coordinate) == solid) {
    return false;
  }
  const bool generated = generator_.generatedSolidAt(coordinate);
  if (solid == generated) {
    overrides_.erase(coordinate);
  } else {
    overrides_[coordinate] = solid;
  }
  Chunk &chunk = ensureChunk(chunkCoordForBlock(coordinate));
  chunk.setSolid(localCoordForBlock(coordinate), solid);
  ++revision_;
  return true;
}

int BlockWorld::surfaceHeightAt(int x, int z, int w) const {
  return generator_.surfaceHeightAt(x, z, w);
}

std::uint32_t BlockWorld::seed() const { return generator_.seed(); }

std::size_t BlockWorld::loadedChunkCount() const { return chunks_.size(); }

std::size_t BlockWorld::maximumLoadedChunks() const {
  return maximumLoadedChunks_;
}

std::uint64_t BlockWorld::revision() const { return revision_; }

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
  BlockCoord current = containingBlock(origin);
  BlockCoord previous = current;
  std::array<int, 4> step{};
  std::array<double, 4> nextBoundary{};
  std::array<double, 4> boundaryStride{};

  for (std::size_t axis = 0; axis < 4; ++axis) {
    const double component = rayDirection[axis];
    if (std::abs(component) <= 1.0e-12) {
      step[axis] = 0;
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
  while (distance <= maximumDistance) {
    if (world.isSolid(current)) {
      return RayHit{current, previous, distance};
    }
    std::size_t selectedAxis = 0;
    for (std::size_t axis = 1; axis < 4; ++axis) {
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
  }
  return std::nullopt;
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
  if (!hit || world.isSolid(hit->placement) ||
      std::find(protectedBlocks.begin(), protectedBlocks.end(),
                hit->placement) != protectedBlocks.end()) {
    return false;
  }
  return world.setSolid(hit->placement, true);
}

} // namespace proj4d
