#pragma once

#include "hypercraft/world/block.hpp"
#include "hypercraft/world/coordinates.hpp"

#include <array>
#include <cstddef>

namespace hypercraft {

class Chunk {
public:
    explicit Chunk(ChunkCoord coord = {});

    ChunkCoord coord() const;
    BlockId block_at(LocalBlockCoord local) const;
    void set_block(LocalBlockCoord local, BlockId block);
    const std::array<BlockId, chunk_volume>& blocks() const;
    void set_blocks(const std::array<BlockId, chunk_volume>& blocks);

private:
    ChunkCoord coord_;
    std::array<BlockId, chunk_volume> blocks_{};
};

} // namespace hypercraft
