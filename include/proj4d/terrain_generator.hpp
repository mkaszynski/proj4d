#pragma once

#include <cstdint>

#include "proj4d/chunk.hpp"

namespace proj4d {

class TerrainGenerator {
public:
  explicit TerrainGenerator(std::uint32_t seed = 0x4D2026U);

  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] double densityAt(const BlockCoord &coordinate) const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord &coordinate) const;
  [[nodiscard]] int surfaceHeightAt(int x, int z, int w) const;
  [[nodiscard]] Chunk generateChunk(const ChunkCoord &coordinate) const;

private:
  [[nodiscard]] double valueNoise4D(double x, double y, double z, double w,
                                    std::uint32_t salt) const;
  [[nodiscard]] double fractalNoise4D(double x, double y, double z,
                                      double w) const;
  [[nodiscard]] double heightGuideAt(int x, int z, int w) const;

  std::uint32_t seed_{};
};

} // namespace proj4d
