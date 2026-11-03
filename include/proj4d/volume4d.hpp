#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "proj4d/render_geometry.hpp"

namespace proj4d {

struct VolumeDisplayCamera {
  double yaw{-0.58};
  double pitch{0.42};
  double distance{3.8};
};

struct ProjectedBoundaryVolume4D {
  std::array<Vec3, 20> vertices{};
  std::size_t vertexCount{8U};
  bool canonicalCube{true};
  BlockId material{BlockId::stone};
  int brightnessPercent{100};
};

struct AveragedVolumeImage {
  int width{};
  int height{};
  std::vector<std::uint8_t> rgba;
};

inline constexpr int maximumVolumeImageWidth = 160;
inline constexpr int maximumProjectedBoundaryVolumes = 8192;

[[nodiscard]] int boundaryBrightnessPercent4D(int axis, int side);

[[nodiscard]] std::vector<ProjectedBoundaryVolume4D>
projectBoundaryVolumes4D(std::span<const VisibleBoundaryCell4D> cells,
                         const Camera4D &camera);
[[nodiscard]] AveragedVolumeImage
renderAveragedVolume4D(std::span<const ProjectedBoundaryVolume4D> volumes,
                       const VolumeDisplayCamera &camera, int displayWidth,
                       int displayHeight,
                       int maximumWidth = maximumVolumeImageWidth);

} // namespace proj4d
