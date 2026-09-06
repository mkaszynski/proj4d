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

double interpolate(double first, double second, double amount) {
  return first + (second - first) * amount;
}

double latticeValue(int x, int y, int z, int w, std::uint32_t seed) {
  constexpr double divisor =
      1.0 / static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  return static_cast<double>(hashLattice(x, y, z, w, seed)) * divisor * 2.0 -
         1.0;
}

std::uint32_t hypercraftHash3D(int x, int y, int z, std::uint32_t seed) {
  std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393U ^
                        static_cast<std::uint32_t>(y) * 668265263U ^
                        static_cast<std::uint32_t>(z) * 2246822519U ^
                        seed * 2246822519U;
  value ^= value >> 13U;
  value *= 3266489917U;
  value ^= value >> 16U;
  return value;
}

std::uint32_t hypercraftHash4D(int x, int y, int z, int w, std::uint32_t seed) {
  std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393U ^
                        static_cast<std::uint32_t>(y) * 668265263U ^
                        static_cast<std::uint32_t>(z) * 2246822519U ^
                        static_cast<std::uint32_t>(w) * 3266489917U ^
                        seed * 2246822519U;
  value ^= value >> 13U;
  value *= 3266489917U;
  value ^= value >> 16U;
  return value;
}

double hypercraftGradientDot3D(int x, int y, int z, double dx, double dy,
                               double dz, std::uint32_t seed) {
  constexpr double diagonal = 0.5773502691896258;
  constexpr std::array<std::array<double, 3>, 12> gradients{{
      {diagonal, diagonal, diagonal},
      {-diagonal, diagonal, diagonal},
      {diagonal, -diagonal, diagonal},
      {diagonal, diagonal, -diagonal},
      {-diagonal, -diagonal, diagonal},
      {-diagonal, diagonal, -diagonal},
      {diagonal, -diagonal, -diagonal},
      {-diagonal, -diagonal, -diagonal},
      {1.0, 0.0, 0.0},
      {-1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
      {0.0, -1.0, 0.0},
  }};
  const auto &gradient =
      gradients[hypercraftHash3D(x, y, z, seed) % gradients.size()];
  return gradient[0] * dx + gradient[1] * dy + gradient[2] * dz;
}

double hypercraftGradientDot4D(int x, int y, int z, int w, double dx, double dy,
                               double dz, double dw, std::uint32_t seed) {
  constexpr double diagonal = 0.5;
  constexpr double diagonal2 = 0.70710678118;
  constexpr std::array<std::array<double, 4>, 32> gradients{{
      {diagonal, diagonal, diagonal, diagonal},
      {-diagonal, diagonal, diagonal, diagonal},
      {diagonal, -diagonal, diagonal, diagonal},
      {diagonal, diagonal, -diagonal, diagonal},
      {diagonal, diagonal, diagonal, -diagonal},
      {-diagonal, -diagonal, diagonal, diagonal},
      {-diagonal, diagonal, -diagonal, diagonal},
      {-diagonal, diagonal, diagonal, -diagonal},
      {diagonal, -diagonal, -diagonal, diagonal},
      {diagonal, -diagonal, diagonal, -diagonal},
      {diagonal, diagonal, -diagonal, -diagonal},
      {-diagonal, -diagonal, -diagonal, diagonal},
      {-diagonal, -diagonal, diagonal, -diagonal},
      {-diagonal, diagonal, -diagonal, -diagonal},
      {diagonal, -diagonal, -diagonal, -diagonal},
      {-diagonal, -diagonal, -diagonal, -diagonal},
      {1.0, 0.0, 0.0, 0.0},
      {-1.0, 0.0, 0.0, 0.0},
      {0.0, 1.0, 0.0, 0.0},
      {0.0, -1.0, 0.0, 0.0},
      {0.0, 0.0, 1.0, 0.0},
      {0.0, 0.0, -1.0, 0.0},
      {0.0, 0.0, 0.0, 1.0},
      {0.0, 0.0, 0.0, -1.0},
      {diagonal2, diagonal2, 0.0, 0.0},
      {diagonal2, 0.0, diagonal2, 0.0},
      {diagonal2, 0.0, 0.0, diagonal2},
      {0.0, diagonal2, diagonal2, 0.0},
      {0.0, diagonal2, 0.0, diagonal2},
      {0.0, 0.0, diagonal2, diagonal2},
      {-diagonal2, diagonal2, 0.0, 0.0},
      {0.0, -diagonal2, 0.0, diagonal2},
  }};
  const auto &gradient =
      gradients[hypercraftHash4D(x, y, z, w, seed) % gradients.size()];
  return gradient[0] * dx + gradient[1] * dy + gradient[2] * dz +
         gradient[3] * dw;
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

double TerrainGenerator::hypercraftPerlinNoise3D(double x, double y, double z,
                                                 std::uint32_t salt) const {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int z0 = static_cast<int>(std::floor(z));
  const double dx = x - static_cast<double>(x0);
  const double dy = y - static_cast<double>(y0);
  const double dz = z - static_cast<double>(z0);
  const double tx = fade(dx);
  const double ty = fade(dy);
  const double tz = fade(dz);
  const std::uint32_t saltedSeed = seed_ ^ (salt * 374761393U);

  const double x00 = interpolate(
      hypercraftGradientDot3D(x0, y0, z0, dx, dy, dz, saltedSeed),
      hypercraftGradientDot3D(x0 + 1, y0, z0, dx - 1.0, dy, dz, saltedSeed),
      tx);
  const double x10 = interpolate(
      hypercraftGradientDot3D(x0, y0 + 1, z0, dx, dy - 1.0, dz, saltedSeed),
      hypercraftGradientDot3D(x0 + 1, y0 + 1, z0, dx - 1.0, dy - 1.0, dz,
                              saltedSeed),
      tx);
  const double x01 = interpolate(
      hypercraftGradientDot3D(x0, y0, z0 + 1, dx, dy, dz - 1.0, saltedSeed),
      hypercraftGradientDot3D(x0 + 1, y0, z0 + 1, dx - 1.0, dy, dz - 1.0,
                              saltedSeed),
      tx);
  const double x11 =
      interpolate(hypercraftGradientDot3D(x0, y0 + 1, z0 + 1, dx, dy - 1.0,
                                          dz - 1.0, saltedSeed),
                  hypercraftGradientDot3D(x0 + 1, y0 + 1, z0 + 1, dx - 1.0,
                                          dy - 1.0, dz - 1.0, saltedSeed),
                  tx);
  return interpolate(interpolate(x00, x10, ty), interpolate(x01, x11, ty), tz);
}

double TerrainGenerator::hypercraftPerlinNoise4D(double x, double y, double z,
                                                 double w,
                                                 std::uint32_t salt) const {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int z0 = static_cast<int>(std::floor(z));
  const int w0 = static_cast<int>(std::floor(w));
  const double dx = x - static_cast<double>(x0);
  const double dy = y - static_cast<double>(y0);
  const double dz = z - static_cast<double>(z0);
  const double dw = w - static_cast<double>(w0);
  const double tx = fade(dx);
  const double ty = fade(dy);
  const double tz = fade(dz);
  const double tw = fade(dw);
  const std::uint32_t saltedSeed = seed_ ^ (salt * 374761393U);
  const auto corner = [saltedSeed](int xi, int yi, int zi, int wi, double ox,
                                   double oy, double oz, double ow) {
    return hypercraftGradientDot4D(xi, yi, zi, wi, ox, oy, oz, ow, saltedSeed);
  };

  const double x000 =
      interpolate(corner(x0, y0, z0, w0, dx, dy, dz, dw),
                  corner(x0 + 1, y0, z0, w0, dx - 1.0, dy, dz, dw), tx);
  const double x100 = interpolate(
      corner(x0, y0 + 1, z0, w0, dx, dy - 1.0, dz, dw),
      corner(x0 + 1, y0 + 1, z0, w0, dx - 1.0, dy - 1.0, dz, dw), tx);
  const double x010 = interpolate(
      corner(x0, y0, z0 + 1, w0, dx, dy, dz - 1.0, dw),
      corner(x0 + 1, y0, z0 + 1, w0, dx - 1.0, dy, dz - 1.0, dw), tx);
  const double x110 = interpolate(
      corner(x0, y0 + 1, z0 + 1, w0, dx, dy - 1.0, dz - 1.0, dw),
      corner(x0 + 1, y0 + 1, z0 + 1, w0, dx - 1.0, dy - 1.0, dz - 1.0, dw), tx);
  const double x001 = interpolate(
      corner(x0, y0, z0, w0 + 1, dx, dy, dz, dw - 1.0),
      corner(x0 + 1, y0, z0, w0 + 1, dx - 1.0, dy, dz, dw - 1.0), tx);
  const double x101 = interpolate(
      corner(x0, y0 + 1, z0, w0 + 1, dx, dy - 1.0, dz, dw - 1.0),
      corner(x0 + 1, y0 + 1, z0, w0 + 1, dx - 1.0, dy - 1.0, dz, dw - 1.0), tx);
  const double x011 = interpolate(
      corner(x0, y0, z0 + 1, w0 + 1, dx, dy, dz - 1.0, dw - 1.0),
      corner(x0 + 1, y0, z0 + 1, w0 + 1, dx - 1.0, dy, dz - 1.0, dw - 1.0), tx);
  const double x111 = interpolate(
      corner(x0, y0 + 1, z0 + 1, w0 + 1, dx, dy - 1.0, dz - 1.0, dw - 1.0),
      corner(x0 + 1, y0 + 1, z0 + 1, w0 + 1, dx - 1.0, dy - 1.0, dz - 1.0,
             dw - 1.0),
      tx);

  const double y00 = interpolate(x000, x100, ty);
  const double y10 = interpolate(x010, x110, ty);
  const double y01 = interpolate(x001, x101, ty);
  const double y11 = interpolate(x011, x111, ty);
  return interpolate(interpolate(y00, y10, tz), interpolate(y01, y11, tz), tw);
}

double TerrainGenerator::hypercraftFractalNoise3D(double x, double y,
                                                  double z) const {
  double amplitude = 1.0;
  double frequency = 1.0;
  double total = 0.0;
  double normalization = 0.0;
  for (std::uint32_t octave = 0; octave < 4U; ++octave) {
    total += hypercraftPerlinNoise3D(x * frequency, y * frequency,
                                     z * frequency, octave) *
             amplitude;
    normalization += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  return total / normalization;
}

double TerrainGenerator::hypercraftFractalNoise4D(double x, double y, double z,
                                                  double w) const {
  double amplitude = 1.0;
  double frequency = 1.0;
  double total = 0.0;
  double normalization = 0.0;
  for (std::uint32_t octave = 0; octave < 4U; ++octave) {
    total += hypercraftPerlinNoise4D(x * frequency, y * frequency,
                                     z * frequency, w * frequency, octave) *
             amplitude;
    normalization += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  return total / normalization;
}

double TerrainGenerator::hypercraftFlatHeightGuideAt(int x, int z,
                                                     int w) const {
  constexpr double terrainZoom = 3.0;
  const double worldX = static_cast<double>(x);
  const double worldZ = static_cast<double>(z);
  const double worldW = static_cast<double>(w);
  const double broadHills =
      hypercraftFractalNoise3D(worldX * 0.035 / terrainZoom,
                               worldZ * 0.035 / terrainZoom,
                               worldW * 0.035 / terrainZoom) *
      18.0;
  const double smallHills =
      hypercraftFractalNoise3D(worldX * 0.11 / terrainZoom + 100.0,
                               worldZ * 0.11 / terrainZoom + 50.0,
                               worldW * 0.11 / terrainZoom - 100.0) *
      5.0;
  return std::round(hypercraftFlatBaseHeight + broadHills + smallHills);
}

double TerrainGenerator::hypercraftFlatDensityAt(const BlockCoord &coordinate,
                                                 double heightGuide) const {
  constexpr double densityZoom = 3.0;
  constexpr double verticalFalloff = 0.045 * 1000.0;
  constexpr double heightFalloff = (0.35 / 90.0) * 1000.0;
  const double x = static_cast<double>(coordinate.x);
  const double y = static_cast<double>(coordinate.y);
  const double z = static_cast<double>(coordinate.z);
  const double w = static_cast<double>(coordinate.w);
  const double aboveBase = std::max(0.0, y - heightGuide);
  const double densityNoise = hypercraftFractalNoise4D(
      x * 0.045 / densityZoom, y * 0.075 / densityZoom + 50.0,
      z * 0.045 / densityZoom, w * 0.045 / densityZoom - 50.0);
  return (heightGuide + 1.0 - y) * verticalFalloff + densityNoise * 16.0 -
         aboveBase * heightFalloff;
}

bool TerrainGenerator::hypercraftFlatSolidAt(const BlockCoord &coordinate,
                                             double heightGuide) const {
  // Hypercraft's 1000x falloff makes every block through the integral height
  // guide solid and every block two levels above it air. Only the intervening
  // level depends on density noise, so avoid evaluating 4D noise elsewhere.
  if (static_cast<double>(coordinate.y) <= heightGuide) {
    return true;
  }
  if (static_cast<double>(coordinate.y) >= heightGuide + 2.0) {
    return false;
  }
  return hypercraftFlatDensityAt(coordinate, heightGuide) > 0.0;
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
  if (mode_ == TerrainMode::Low) {
    return hypercraftFlatSolidAt(
        coordinate,
        hypercraftFlatHeightGuideAt(coordinate.x, coordinate.z, coordinate.w));
  }
  return coordinate.y <= flatGroundSurfaceY;
}

int TerrainGenerator::surfaceHeightAt(int x, int z, int w) const {
  if (mode_ == TerrainMode::Flat) {
    return flatGroundSurfaceY;
  }
  if (mode_ == TerrainMode::Low) {
    const double heightGuide = hypercraftFlatHeightGuideAt(x, z, w);
    const int baseHeight = static_cast<int>(heightGuide);
    return hypercraftFlatSolidAt({x, baseHeight + 1, z, w}, heightGuide)
               ? baseHeight + 1
               : baseHeight;
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
  if (mode_ == TerrainMode::Low) {
    for (int w = 0; w < chunkSize; ++w) {
      for (int z = 0; z < chunkSize; ++z) {
        for (int x = 0; x < chunkSize; ++x) {
          const BlockCoord column =
              blockCoordFromChunkLocal(coordinate, LocalBlockCoord{x, 0, z, w});
          const double heightGuide =
              hypercraftFlatHeightGuideAt(column.x, column.z, column.w);
          for (int y = 0; y < chunkSize; ++y) {
            const LocalBlockCoord local{x, y, z, w};
            const BlockCoord block =
                blockCoordFromChunkLocal(coordinate, local);
            chunk.setSolid(local, hypercraftFlatSolidAt(block, heightGuide));
          }
        }
      }
    }
    return chunk;
  }
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
