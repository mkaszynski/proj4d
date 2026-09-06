#pragma once

#include <array>
#include <compare>
#include <cstddef>

namespace proj4d {

inline constexpr int chunkSize = 16;
inline constexpr int chunkVolume =
    chunkSize * chunkSize * chunkSize * chunkSize;

struct BlockCoord {
  int x{};
  int y{};
  int z{};
  int w{};

  [[nodiscard]] int &operator[](std::size_t index) {
    return *std::array<int *, 4>{&x, &y, &z, &w}.at(index);
  }
  [[nodiscard]] int operator[](std::size_t index) const {
    return std::array<int, 4>{x, y, z, w}.at(index);
  }
  [[nodiscard]] auto operator<=>(const BlockCoord &) const = default;
};

struct ChunkCoord {
  int x{};
  int y{};
  int z{};
  int w{};

  [[nodiscard]] auto operator<=>(const ChunkCoord &) const = default;
};

struct LocalBlockCoord {
  int x{};
  int y{};
  int z{};
  int w{};

  [[nodiscard]] auto operator<=>(const LocalBlockCoord &) const = default;
};

struct ChunkCoordHash {
  [[nodiscard]] std::size_t operator()(const ChunkCoord &coordinate) const;
};

struct BlockCoordHash {
  [[nodiscard]] std::size_t operator()(const BlockCoord &coordinate) const;
};

[[nodiscard]] int floorDiv(int value, int divisor);
[[nodiscard]] int floorMod(int value, int divisor);
[[nodiscard]] ChunkCoord chunkCoordForBlock(const BlockCoord &block);
[[nodiscard]] LocalBlockCoord localCoordForBlock(const BlockCoord &block);
[[nodiscard]] BlockCoord blockCoordFromChunkLocal(const ChunkCoord &chunk,
                                                  const LocalBlockCoord &local);
[[nodiscard]] std::size_t localBlockIndex(const LocalBlockCoord &local);

} // namespace proj4d
