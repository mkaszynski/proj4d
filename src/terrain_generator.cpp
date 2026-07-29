#include "proj4d/terrain_generator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace proj4d {

namespace {

std::uint32_t avalanche(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7FEB352DU;
  value ^= value >> 15U;
  value *= 0x846CA68BU;
  value ^= value >> 16U;
  return value;
}

std::uint32_t hashLattice(int x, int y, int z, int w, std::uint32_t seed) {
  std::uint32_t hash = seed ^ 0x9E3779B9U;
  for (const int coordinate : std::array<int, 4>{x, y, z, w}) {
    hash ^= avalanche(static_cast<std::uint32_t>(coordinate));
    hash = avalanche(hash + 0x85EBCA6BU);
  }
  return hash;
}

double fade(double value) {
  return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

double latticeValue(int x, int y, int z, int w, std::uint32_t seed) {
  constexpr double divisor =
      1.0 / static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  return static_cast<double>(hashLattice(x, y, z, w, seed)) * divisor * 2.0 -
         1.0;
}

} // namespace

TerrainGenerator::TerrainGenerator(std::uint32_t seed, TerrainMode mode)
    : seed_(seed), mode_(mode) {}

std::uint32_t TerrainGenerator::seed() const { return seed_; }

TerrainMode TerrainGenerator::mode() const { return mode_; }

double TerrainGenerator::valueNoise4D(double x, double y, double z, double w,
                                      std::uint32_t salt) const {
  const std::array<double, 4> coordinates{x, y, z, w};
  std::array<int, 4> lattice{};
  std::array<double, 4> interpolation{};
  for (std::size_t axis = 0; axis < coordinates.size(); ++axis) {
    lattice[axis] = static_cast<int>(std::floor(coordinates[axis]));
    interpolation[axis] =
        fade(coordinates[axis] - static_cast<double>(lattice[axis]));
  }

  double result = 0.0;
  for (int corner = 0; corner < 16; ++corner) {
    std::array<int, 4> sample = lattice;
    double weight = 1.0;
    for (std::size_t axis = 0; axis < sample.size(); ++axis) {
      const bool upper = (corner & (1 << axis)) != 0;
      if (upper) {
        ++sample[axis];
        weight *= interpolation[axis];
      } else {
        weight *= 1.0 - interpolation[axis];
      }
    }
    result +=
        latticeValue(sample[0], sample[1], sample[2], sample[3], seed_ ^ salt) *
        weight;
  }
  return result;
}

double TerrainGenerator::fractalNoise4D(double x, double y, double z,
                                        double w) const {
  double total = 0.0;
  double amplitude = 1.0;
  double normalization = 0.0;
  double frequency = 1.0;
  for (std::uint32_t octave = 0; octave < 3U; ++octave) {
    total += valueNoise4D(x * frequency, y * frequency, z * frequency,
                          w * frequency, 0xA511E9B3U + octave * 0x9E3779B9U) *
             amplitude;
    normalization += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  return total / normalization;
}

double TerrainGenerator::heightGuideAt(int x, int z, int w) const {
  const double broad = valueNoise4D(
      static_cast<double>(x) * 0.018, 37.0, static_cast<double>(z) * 0.018,
      static_cast<double>(w) * 0.018, 0xB04D4D11U);
  const double detail = valueNoise4D(
      static_cast<double>(x) * 0.055, -19.0, static_cast<double>(z) * 0.055,
      static_cast<double>(w) * 0.055, 0xD37A1105U);
  return 3.0 + broad * 8.0 + detail * 2.5;
}

double TerrainGenerator::densityAt(const BlockCoord &coordinate) const {
  const double x = static_cast<double>(coordinate.x);
  const double y = static_cast<double>(coordinate.y);
  const double z = static_cast<double>(coordinate.z);
  const double w = static_cast<double>(coordinate.w);
  const double terrainNoise =
      fractalNoise4D(x * 0.055, y * 0.095 + 41.0, z * 0.055, w * 0.055 - 23.0);
  return (heightGuideAt(coordinate.x, coordinate.z, coordinate.w) - y) * 0.18 +
         terrainNoise * 4.2;
}

bool TerrainGenerator::generatedSolidAt(const BlockCoord &coordinate) const {
  if (mode_ == TerrainMode::Density) {
    return densityAt(coordinate) > 0.0;
  }
  const int surface =
      mode_ == TerrainMode::Low ? lowGroundSurfaceY : flatGroundSurfaceY;
  return coordinate.y <= surface;
}

int TerrainGenerator::surfaceHeightAt(int x, int z, int w) const {
  if (mode_ != TerrainMode::Density) {
    return mode_ == TerrainMode::Low ? lowGroundSurfaceY : flatGroundSurfaceY;
  }
  constexpr int maximumSurfaceSearch = 96;
  constexpr int minimumSurfaceSearch = -96;
  for (int y = maximumSurfaceSearch; y >= minimumSurfaceSearch; --y) {
    if (generatedSolidAt({x, y, z, w})) {
      return y;
    }
  }
  return minimumSurfaceSearch;
}

Chunk TerrainGenerator::generateChunk(const ChunkCoord &coordinate) const {
  Chunk chunk(coordinate);
  for (int w = 0; w < chunkSize; ++w) {
    for (int y = 0; y < chunkSize; ++y) {
      for (int z = 0; z < chunkSize; ++z) {
        for (int x = 0; x < chunkSize; ++x) {
          const LocalBlockCoord local{x, y, z, w};
          const BlockCoord block = blockCoordFromChunkLocal(coordinate, local);
          chunk.setSolid(local, generatedSolidAt(block));
        }
      }
    }
  }
  return chunk;
}

} // namespace proj4d
