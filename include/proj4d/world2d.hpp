#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

#include "proj4d/math.hpp"
#include "proj4d/terrain_generator.hpp"

namespace proj4d {

inline constexpr int chunkSize2D = 16;
inline constexpr int chunkVolume2D = chunkSize2D * chunkSize2D;

struct BlockCoord2D {
  int x{};
  int y{};

  [[nodiscard]] int &operator[](std::size_t index) {
    return *std::array<int *, 2>{&x, &y}.at(index);
  }
  [[nodiscard]] int operator[](std::size_t index) const {
    return std::array<int, 2>{x, y}.at(index);
  }
  [[nodiscard]] auto operator<=>(const BlockCoord2D &) const = default;
};

struct ChunkCoord2D {
  int x{};
  int y{};
  [[nodiscard]] auto operator<=>(const ChunkCoord2D &) const = default;
};

struct LocalBlockCoord2D {
  int x{};
  int y{};
  [[nodiscard]] auto operator<=>(const LocalBlockCoord2D &) const = default;
};

struct ChunkCoord2DHash {
  [[nodiscard]] std::size_t operator()(const ChunkCoord2D &coordinate) const;
};

struct BlockCoord2DHash {
  [[nodiscard]] std::size_t operator()(const BlockCoord2D &coordinate) const;
};

[[nodiscard]] ChunkCoord2D chunkCoordForBlock(const BlockCoord2D &block);
[[nodiscard]] LocalBlockCoord2D localCoordForBlock(const BlockCoord2D &block);
[[nodiscard]] BlockCoord2D
blockCoordFromChunkLocal(const ChunkCoord2D &chunk,
                         const LocalBlockCoord2D &local);
[[nodiscard]] std::size_t localBlockIndex(const LocalBlockCoord2D &local);

class Chunk2D {
public:
  explicit Chunk2D(ChunkCoord2D coordinate = {});

  [[nodiscard]] const ChunkCoord2D &coordinate() const;
  [[nodiscard]] bool isSolid(const LocalBlockCoord2D &local) const;
  void setSolid(const LocalBlockCoord2D &local, bool solid);

private:
  static constexpr std::size_t wordBits = 64;
  static constexpr std::size_t wordCount =
      static_cast<std::size_t>(chunkVolume2D) / wordBits;

  ChunkCoord2D coordinate_{};
  std::array<std::uint64_t, wordCount> blocks_{};
};

class TerrainGenerator2D {
public:
  explicit TerrainGenerator2D(std::uint32_t seed = 0x4D2026U,
                              TerrainMode mode = TerrainMode::Flat);

  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] TerrainMode mode() const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord2D &coordinate) const;
  [[nodiscard]] int surfaceHeightAt(int x) const;
  [[nodiscard]] Chunk2D generateChunk(const ChunkCoord2D &coordinate) const;

private:
  TerrainGenerator crossSectionGenerator_;
};

class BlockWorld2D {
public:
  explicit BlockWorld2D(std::uint32_t seed = 0x4D2026U,
                        std::size_t maximumLoadedChunks = 96U);
  explicit BlockWorld2D(TerrainMode terrainMode, std::uint32_t seed = 0x4D2026U,
                        std::size_t maximumLoadedChunks = 96U);

  [[nodiscard]] bool isSolid(const BlockCoord2D &coordinate) const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord2D &coordinate) const;
  [[nodiscard]] bool setSolid(const BlockCoord2D &coordinate, bool solid);
  [[nodiscard]] int surfaceHeightAt(int x) const;
  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] TerrainMode terrainMode() const;
  [[nodiscard]] std::size_t loadedChunkCount() const;
  [[nodiscard]] std::size_t maximumLoadedChunks() const;
  [[nodiscard]] std::uint64_t revision() const;

private:
  struct CachedChunk {
    Chunk2D chunk;
    std::uint64_t lastAccess{};
  };

  [[nodiscard]] Chunk2D &ensureChunk(const ChunkCoord2D &coordinate) const;
  void evictLeastRecentlyUsed(const ChunkCoord2D &protectedChunk) const;

  TerrainGenerator2D generator_;
  std::size_t maximumLoadedChunks_{};
  mutable std::uint64_t accessClock_{};
  mutable std::unordered_map<ChunkCoord2D, CachedChunk, ChunkCoord2DHash>
      chunks_;
  std::unordered_map<BlockCoord2D, bool, BlockCoord2DHash> overrides_;
  std::uint64_t revision_{};
};

struct RayHit2D {
  BlockCoord2D block{};
  BlockCoord2D placement{};
  double distance{};
  int boundaryNormalAxis{-1};
};

[[nodiscard]] BlockCoord2D containingBlock(const Vec2 &point);
[[nodiscard]] std::optional<RayHit2D> raycast(const BlockWorld2D &world,
                                              const Vec2 &origin,
                                              const Vec2 &direction,
                                              double maximumDistance);
[[nodiscard]] bool breakAlongRay(BlockWorld2D &world, const Vec2 &origin,
                                 const Vec2 &direction, double maximumDistance);
[[nodiscard]] bool buildAlongRay(BlockWorld2D &world, const Vec2 &origin,
                                 const Vec2 &direction, double maximumDistance,
                                 std::span<const BlockCoord2D> protectedBlocks);

} // namespace proj4d
