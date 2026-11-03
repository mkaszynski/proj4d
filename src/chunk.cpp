#include "proj4d/chunk.hpp"

namespace proj4d {

Chunk::Chunk(ChunkCoord coordinate) : coordinate_(coordinate) {}

const ChunkCoord &Chunk::coordinate() const { return coordinate_; }

BlockId Chunk::blockAt(const LocalBlockCoord &local) const {
  return blocks_.at(localBlockIndex(local));
}

void Chunk::setBlock(const LocalBlockCoord &local, BlockId block) {
  blocks_.at(localBlockIndex(local)) = block;
}

bool Chunk::isSolid(const LocalBlockCoord &local) const {
  return proj4d::isSolid(blockAt(local));
}

void Chunk::setSolid(const LocalBlockCoord &local, bool solid) {
  setBlock(local, solid ? BlockId::stone : BlockId::air);
}

} // namespace proj4d
