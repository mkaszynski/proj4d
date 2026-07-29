#pragma once

#include <array>
#include <cstdint>

#include "proj4d/coordinates.hpp"

namespace proj4d {

class Chunk {
public:
  explicit Chunk(ChunkCoord coordinate = {});

  [[nodiscard]] const ChunkCoord &coordinate() const;
  [[nodiscard]] bool isSolid(const LocalBlockCoord &local) const;
  void setSolid(const LocalBlockCoord &local, bool solid);

private:
  static constexpr std::size_t wordBits = 64;
  static constexpr std::size_t wordCount =
      static_cast<std::size_t>(chunkVolume) / wordBits;

  ChunkCoord coordinate_{};
  std::array<std::uint64_t, wordCount> blocks_{};
};

} // namespace proj4d
