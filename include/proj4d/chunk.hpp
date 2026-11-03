#pragma once

#include "proj4d/content.hpp"
#include "proj4d/coordinates.hpp"
#include <array>

namespace proj4d {

class Chunk {
public:
  explicit Chunk(ChunkCoord coordinate = {});

  [[nodiscard]] const ChunkCoord &coordinate() const;
  [[nodiscard]] BlockId blockAt(const LocalBlockCoord &local) const;
  void setBlock(const LocalBlockCoord &local, BlockId block);
  [[nodiscard]] bool isSolid(const LocalBlockCoord &local) const;
  void setSolid(const LocalBlockCoord &local, bool solid);

private:
  ChunkCoord coordinate_{};
  std::array<BlockId, static_cast<std::size_t>(chunkVolume)> blocks_{};
};

} // namespace proj4d
