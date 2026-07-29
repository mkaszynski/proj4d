#include "proj4d/coordinates.hpp"

#include <cstdint>

namespace proj4d {

namespace {

void mixCoordinate(std::uint64_t &hash, int value) {
  hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(value) +
                                     0x9E3779B9LL);
  hash *= 1099511628211ULL;
}

std::size_t finishHash(std::uint64_t hash) {
  if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
    return static_cast<std::size_t>(hash);
  }
  return static_cast<std::size_t>(hash ^ (hash >> 32U));
}

} // namespace

std::size_t ChunkCoordHash::operator()(const ChunkCoord &coordinate) const {
  std::uint64_t hash = 1469598103934665603ULL;
  mixCoordinate(hash, coordinate.x);
  mixCoordinate(hash, coordinate.y);
  mixCoordinate(hash, coordinate.z);
  mixCoordinate(hash, coordinate.w);
  return finishHash(hash);
}

std::size_t BlockCoordHash::operator()(const BlockCoord &coordinate) const {
  std::uint64_t hash = 1469598103934665603ULL;
  mixCoordinate(hash, coordinate.x);
  mixCoordinate(hash, coordinate.y);
  mixCoordinate(hash, coordinate.z);
  mixCoordinate(hash, coordinate.w);
  return finishHash(hash);
}

int floorDiv(int value, int divisor) {
  int result = value / divisor;
  const int remainder = value % divisor;
  if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
    --result;
  }
  return result;
}

int floorMod(int value, int divisor) {
  const int result = value % divisor;
  return result < 0 ? result + divisor : result;
}

ChunkCoord chunkCoordForBlock(const BlockCoord &block) {
  return {
      floorDiv(block.x, chunkSize),
      floorDiv(block.y, chunkSize),
      floorDiv(block.z, chunkSize),
      floorDiv(block.w, chunkSize),
  };
}

LocalBlockCoord localCoordForBlock(const BlockCoord &block) {
  return {
      floorMod(block.x, chunkSize),
      floorMod(block.y, chunkSize),
      floorMod(block.z, chunkSize),
      floorMod(block.w, chunkSize),
  };
}

BlockCoord blockCoordFromChunkLocal(const ChunkCoord &chunk,
                                    const LocalBlockCoord &local) {
  return {
      chunk.x * chunkSize + local.x,
      chunk.y * chunkSize + local.y,
      chunk.z * chunkSize + local.z,
      chunk.w * chunkSize + local.w,
  };
}

std::size_t localBlockIndex(const LocalBlockCoord &local) {
  return static_cast<std::size_t>(
      local.x +
      chunkSize * (local.z + chunkSize * (local.y + chunkSize * local.w)));
}

} // namespace proj4d
