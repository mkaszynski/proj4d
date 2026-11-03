#include "proj4d/clouds.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace proj4d {

namespace {

int cellCoordinate(double value) {
  return static_cast<int>(std::floor(value / cloudCellSize));
}

std::uint32_t mix(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7FEB352DU;
  value ^= value >> 15U;
  value *= 0x846CA68BU;
  return value ^ (value >> 16U);
}

Vec4 cloudBoundaryVertex(const Cloud4D &cloud, int axis, int side, int mask) {
  Vec4 point = cloud.minimum;
  point[static_cast<std::size_t>(axis)] += side > 0 ? cloud.size : 0.0;
  int bit = 0;
  for (int tangent = 0; tangent < 4; ++tangent) {
    if (tangent == axis) {
      continue;
    }
    if ((mask & (1 << bit)) != 0) {
      point[static_cast<std::size_t>(tangent)] += cloud.size;
    }
    ++bit;
  }
  return point;
}

} // namespace

bool cloudCellAt(int x, int z, int w, std::uint32_t seed) {
  std::uint32_t hash = seed;
  hash = mix(hash ^ static_cast<std::uint32_t>(x) * 0x9E3779B9U);
  hash = mix(hash ^ static_cast<std::uint32_t>(z) * 0x85EBCA6BU);
  hash = mix(hash ^ static_cast<std::uint32_t>(w) * 0xC2B2AE35U);
  return hash % 1000U < 220U;
}

std::vector<Cloud4D> visibleClouds4D(const Vec4 &position, int radius) {
  radius = std::max(0, radius);
  const int centerX = cellCoordinate(position.x);
  const int centerZ = cellCoordinate(position.z);
  const int centerW = cellCoordinate(position.w);
  std::vector<Cloud4D> clouds;
  for (int w = centerW - radius; w <= centerW + radius; ++w) {
    for (int z = centerZ - radius; z <= centerZ + radius; ++z) {
      for (int x = centerX - radius; x <= centerX + radius; ++x) {
        if (cloudCellAt(x, z, w)) {
          clouds.push_back({{x * cloudCellSize, cloudAltitude,
                             z * cloudCellSize, w * cloudCellSize},
                            cloudCellSize});
        }
      }
    }
  }
  return clouds;
}

std::vector<Cloud3D> visibleClouds3D(const Vec3 &position, int radius) {
  radius = std::max(0, radius);
  const int centerX = cellCoordinate(position.x);
  const int centerZ = cellCoordinate(position.z);
  std::vector<Cloud3D> clouds;
  for (int z = centerZ - radius; z <= centerZ + radius; ++z) {
    for (int x = centerX - radius; x <= centerX + radius; ++x) {
      if (cloudCellAt(x, z, 0)) {
        clouds.push_back({{x * cloudCellSize, cloudAltitude, z * cloudCellSize},
                          cloudCellSize});
      }
    }
  }
  return clouds;
}

std::vector<Cloud2D> visibleClouds2D(const Vec2 &position, int radius) {
  radius = std::max(0, radius);
  const int centerX = cellCoordinate(position.x);
  std::vector<Cloud2D> clouds;
  for (int x = centerX - radius; x <= centerX + radius; ++x) {
    if (cloudCellAt(x, 0, 0)) {
      clouds.push_back({{x * cloudCellSize, cloudAltitude}, cloudCellSize});
    }
  }
  return clouds;
}

std::vector<ProjectedBoundaryVolume4D>
projectCloudVolumes4D(const std::vector<Cloud4D> &clouds,
                      const Camera4D &camera) {
  std::vector<ProjectedBoundaryVolume4D> volumes;
  volumes.reserve(clouds.size() * 8U);
  for (const Cloud4D &cloud : clouds) {
    for (int axis = 0; axis < 4; ++axis) {
      for (const int side : {-1, 1}) {
        ProjectedBoundaryVolume4D volume;
        volume.material = BlockId::snow;
        volume.brightnessPercent = boundaryBrightnessPercent4D(axis, side);
        bool complete = true;
        for (int mask = 0; mask < 8; ++mask) {
          const auto projected =
              camera.project(cloudBoundaryVertex(cloud, axis, side, mask));
          if (!projected) {
            complete = false;
            break;
          }
          volume.vertices[static_cast<std::size_t>(mask)] = projected->position;
        }
        if (complete) {
          volumes.push_back(volume);
        }
      }
    }
  }
  return volumes;
}

} // namespace proj4d
