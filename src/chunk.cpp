#include "proj4d/chunk.hpp"

namespace proj4d {

Chunk::Chunk(ChunkCoord coordinate) : coordinate_(coordinate) {}

const ChunkCoord &Chunk::coordinate() const { return coordinate_; }

bool Chunk::isSolid(const LocalBlockCoord &local) const {
  const std::size_t index = localBlockIndex(local);
  const std::size_t word = index / wordBits;
  const std::size_t bit = index % wordBits;
  return (blocks_[word] & (std::uint64_t{1} << bit)) != 0U;
}

void Chunk::setSolid(const LocalBlockCoord &local, bool solid) {
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

} // namespace proj4d
