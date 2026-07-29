#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "proj4d/math.hpp"
#include "proj4d/terrain_generator.hpp"

namespace proj4d {

struct RayHit {
  BlockCoord block{};
  BlockCoord placement{};
  double distance{};
};

class BlockWorld {
public:
  explicit BlockWorld(std::uint32_t seed = 0x4D2026U,
                      std::size_t maximumLoadedChunks = 96U);
  explicit BlockWorld(TerrainMode terrainMode, std::uint32_t seed = 0x4D2026U,
                      std::size_t maximumLoadedChunks = 96U);

  [[nodiscard]] bool isSolid(const BlockCoord &coordinate) const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord &coordinate) const;
  [[nodiscard]] bool setSolid(const BlockCoord &coordinate, bool solid);
  [[nodiscard]] int surfaceHeightAt(int x, int z, int w) const;
  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] TerrainMode terrainMode() const;
  [[nodiscard]] std::size_t loadedChunkCount() const;
  [[nodiscard]] std::size_t maximumLoadedChunks() const;
  [[nodiscard]] std::uint64_t revision() const;

private:
  struct CachedChunk {
    Chunk chunk;
    std::uint64_t lastAccess{};
  };

  [[nodiscard]] Chunk &ensureChunk(const ChunkCoord &coordinate) const;
  void evictLeastRecentlyUsed(const ChunkCoord &protectedChunk) const;

  TerrainGenerator generator_;
  std::size_t maximumLoadedChunks_{};
  mutable std::uint64_t accessClock_{};
  mutable std::unordered_map<ChunkCoord, CachedChunk, ChunkCoordHash> chunks_;
  std::unordered_map<BlockCoord, bool, BlockCoordHash> overrides_;
  std::uint64_t revision_{};
};

[[nodiscard]] BlockCoord containingBlock(const Vec4 &point);
[[nodiscard]] std::optional<RayHit> raycast(const BlockWorld &world,
                                            const Vec4 &origin,
                                            const Vec4 &direction,
                                            double maximumDistance);
[[nodiscard]] bool breakAlongRay(BlockWorld &world, const Vec4 &origin,
                                 const Vec4 &direction, double maximumDistance);
[[nodiscard]] bool buildAlongRay(BlockWorld &world, const Vec4 &origin,
                                 const Vec4 &direction, double maximumDistance,
                                 std::span<const BlockCoord> protectedBlocks);

} // namespace proj4d
