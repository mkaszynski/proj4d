#pragma once

#include <cstdint>
#include <unordered_map>

#include "hypercraft/world/terrain_generator.hpp"
#include "proj4d/chunk.hpp"

namespace proj4d {

inline constexpr int flatGroundSurfaceY = 0;
inline constexpr double hypercraftFlatBaseHeight = 18.0;

enum class TerrainMode {
  Flat,
  Low,
  Density,
};

class TerrainGenerator {
public:
  explicit TerrainGenerator(std::uint32_t seed = 0x4D2026U,
                            TerrainMode mode = TerrainMode::Flat);

  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] TerrainMode mode() const;
  [[nodiscard]] double densityAt(const BlockCoord &coordinate) const;
  [[nodiscard]] BlockId generatedBlockAt(const BlockCoord &coordinate) const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord &coordinate) const;
  [[nodiscard]] int surfaceHeightAt(int x, int z, int w) const;
  [[nodiscard]] Chunk generateChunk(const ChunkCoord &coordinate) const;

private:
  [[nodiscard]] double valueNoise4D(double x, double y, double z, double w,
                                    std::uint32_t salt) const;
  [[nodiscard]] double fractalNoise4D(double x, double y, double z,
                                      double w) const;
  [[nodiscard]] double heightGuideAt(int x, int z, int w) const;
  [[nodiscard]] double hypercraftPerlinNoise3D(double x, double y, double z,
                                               std::uint32_t salt) const;
  [[nodiscard]] double hypercraftPerlinNoise4D(double x, double y, double z,
                                               double w,
                                               std::uint32_t salt) const;
  [[nodiscard]] double hypercraftFractalNoise3D(double x, double y,
                                                double z) const;
  [[nodiscard]] double hypercraftFractalNoise4D(double x, double y, double z,
                                                double w) const;
  [[nodiscard]] double hypercraftFlatHeightGuideAt(int x, int z, int w) const;
  [[nodiscard]] double hypercraftFlatDensityAt(const BlockCoord &coordinate,
                                               double heightGuide) const;
  [[nodiscard]] bool hypercraftFlatSolidAt(const BlockCoord &coordinate,
                                           double heightGuide) const;
  [[nodiscard]] BlockId flatOreBlockAt(const BlockCoord &coordinate) const;
  [[nodiscard]] const Chunk &
  ensureDecoratedChunk(const ChunkCoord &coordinate) const;

  std::uint32_t seed_{};
  TerrainMode mode_{};
  hypercraft::TerrainGenerator lowGenerator_;
  hypercraft::TerrainGenerator highGenerator_;
  struct CachedGeneratedChunk {
    Chunk chunk;
    std::uint64_t lastAccess{};
  };
  mutable std::uint64_t generatedChunkAccessClock_{};
  mutable std::unordered_map<ChunkCoord, CachedGeneratedChunk, ChunkCoordHash>
      generatedChunkCache_;
};

} // namespace proj4d
