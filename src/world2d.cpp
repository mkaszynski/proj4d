#include "proj4d/world2d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "proj4d/coordinates.hpp"

namespace proj4d {

namespace {

void mixCoordinate(std::uint64_t &hash, int value) {
  hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(value) +
                                     0x9E3779B9LL);
  hash *= 1099511628211ULL;
}

std::size_t hashCoordinates(int x, int y) {
  std::uint64_t hash = 1469598103934665603ULL;
  mixCoordinate(hash, x);
  mixCoordinate(hash, y);
  if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
    return static_cast<std::size_t>(hash);
  }
  return static_cast<std::size_t>(hash ^ (hash >> 32U));
}

} // namespace

std::size_t ChunkCoord2DHash::operator()(const ChunkCoord2D &coordinate) const {
  return hashCoordinates(coordinate.x, coordinate.y);
}

std::size_t BlockCoord2DHash::operator()(const BlockCoord2D &coordinate) const {
  return hashCoordinates(coordinate.x, coordinate.y);
}

ChunkCoord2D chunkCoordForBlock(const BlockCoord2D &block) {
  return {floorDiv(block.x, chunkSize2D), floorDiv(block.y, chunkSize2D)};
}

LocalBlockCoord2D localCoordForBlock(const BlockCoord2D &block) {
  return {floorMod(block.x, chunkSize2D), floorMod(block.y, chunkSize2D)};
}

BlockCoord2D blockCoordFromChunkLocal(const ChunkCoord2D &chunk,
                                      const LocalBlockCoord2D &local) {
  return {chunk.x * chunkSize2D + local.x, chunk.y * chunkSize2D + local.y};
}

std::size_t localBlockIndex(const LocalBlockCoord2D &local) {
  return static_cast<std::size_t>(local.x + chunkSize2D * local.y);
}

Chunk2D::Chunk2D(ChunkCoord2D coordinate) : coordinate_(coordinate) {}

const ChunkCoord2D &Chunk2D::coordinate() const { return coordinate_; }

bool Chunk2D::isSolid(const LocalBlockCoord2D &local) const {
  const std::size_t index = localBlockIndex(local);
  const std::size_t word = index / wordBits;
  const std::size_t bit = index % wordBits;
  return (blocks_[word] & (std::uint64_t{1} << bit)) != 0U;
}

void Chunk2D::setSolid(const LocalBlockCoord2D &local, bool solid) {
  const std::size_t index = localBlockIndex(local);
  const std::size_t word = index / wordBits;
  const std::size_t bit = index % wordBits;
  const std::uint64_t mask = std::uint64_t{1} << bit;
  if (solid) {
    blocks_[word] |= mask;
  } else {
    blocks_[word] &= ~mask;
  }
}

TerrainGenerator2D::TerrainGenerator2D(std::uint32_t seed, TerrainMode mode)
    : crossSectionGenerator_(seed, mode) {}

std::uint32_t TerrainGenerator2D::seed() const {
  return crossSectionGenerator_.seed();
}

TerrainMode TerrainGenerator2D::mode() const {
  return crossSectionGenerator_.mode();
}

bool TerrainGenerator2D::generatedSolidAt(
    const BlockCoord2D &coordinate) const {
  return crossSectionGenerator_.generatedSolidAt(
      {coordinate.x, coordinate.y, 0, 0});
}

int TerrainGenerator2D::surfaceHeightAt(int x) const {
  return crossSectionGenerator_.surfaceHeightAt(x, 0, 0);
}

Chunk2D
TerrainGenerator2D::generateChunk(const ChunkCoord2D &coordinate) const {
  Chunk2D chunk(coordinate);
  for (int y = 0; y < chunkSize2D; ++y) {
    for (int x = 0; x < chunkSize2D; ++x) {
      const LocalBlockCoord2D local{x, y};
      chunk.setSolid(
          local, generatedSolidAt(blockCoordFromChunkLocal(coordinate, local)));
    }
  }
  return chunk;
}

BlockWorld2D::BlockWorld2D(std::uint32_t seed, std::size_t maximumLoadedChunks)
    : BlockWorld2D(TerrainMode::Flat, seed, maximumLoadedChunks) {}

BlockWorld2D::BlockWorld2D(TerrainMode terrainMode, std::uint32_t seed,
                           std::size_t maximumLoadedChunks)
    : generator_(seed, terrainMode),
      maximumLoadedChunks_(std::max<std::size_t>(1U, maximumLoadedChunks)) {}

Chunk2D &BlockWorld2D::ensureChunk(const ChunkCoord2D &coordinate) const {
  if (auto existing = chunks_.find(coordinate); existing != chunks_.end()) {
    existing->second.lastAccess = ++accessClock_;
    return existing->second.chunk;
  }

  Chunk2D generated = generator_.generateChunk(coordinate);
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

void BlockWorld2D::evictLeastRecentlyUsed(
    const ChunkCoord2D &protectedChunk) const {
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

bool BlockWorld2D::isSolid(const BlockCoord2D &coordinate) const {
  Chunk2D &chunk = ensureChunk(chunkCoordForBlock(coordinate));
  return chunk.isSolid(localCoordForBlock(coordinate));
}

bool BlockWorld2D::generatedSolidAt(const BlockCoord2D &coordinate) const {
  return generator_.generatedSolidAt(coordinate);
}

bool BlockWorld2D::setSolid(const BlockCoord2D &coordinate, bool solid) {
  if (isSolid(coordinate) == solid) {
    return false;
  }
  const bool generated = generator_.generatedSolidAt(coordinate);
  if (solid == generated) {
    overrides_.erase(coordinate);
  } else {
    overrides_[coordinate] = solid;
  }
  ensureChunk(chunkCoordForBlock(coordinate))
      .setSolid(localCoordForBlock(coordinate), solid);
  ++revision_;
  return true;
}

int BlockWorld2D::surfaceHeightAt(int x) const {
  return generator_.surfaceHeightAt(x);
}

std::uint32_t BlockWorld2D::seed() const { return generator_.seed(); }

TerrainMode BlockWorld2D::terrainMode() const { return generator_.mode(); }

std::size_t BlockWorld2D::loadedChunkCount() const { return chunks_.size(); }

std::size_t BlockWorld2D::maximumLoadedChunks() const {
  return maximumLoadedChunks_;
}

std::uint64_t BlockWorld2D::revision() const { return revision_; }

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
