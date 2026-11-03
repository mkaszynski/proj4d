#include "hypercraft/world/chunk.hpp"

namespace hypercraft {

Chunk::Chunk(ChunkCoord coord)
    : coord_(coord)
{
    blocks_.fill(BlockId::air);
}

ChunkCoord Chunk::coord() const
{
    return coord_;
}

BlockId Chunk::block_at(LocalBlockCoord local) const
{
    return blocks_[local_block_index(local)];
}

void Chunk::set_block(LocalBlockCoord local, BlockId block)
{
    blocks_[local_block_index(local)] = block;
}

const std::array<BlockId, chunk_volume>& Chunk::blocks() const
{
    return blocks_;
}

void Chunk::set_blocks(const std::array<BlockId, chunk_volume>& blocks)
{
    blocks_ = blocks;
}

} // namespace hypercraft
