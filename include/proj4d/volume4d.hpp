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

enum class VolumeViewMode { Solid, CenterSlice };

[[nodiscard]] constexpr VolumeViewMode
toggledVolumeViewMode(VolumeViewMode current) {
  return current == VolumeViewMode::Solid ? VolumeViewMode::CenterSlice
                                          : VolumeViewMode::Solid;
}

struct ProjectedBoundaryVolume4D {
  std::array<Vec3, 20> vertices{};
  std::size_t vertexCount{8U};
  bool canonicalCube{true};
  BlockId material{BlockId::stone};
  int brightnessPercent{100};
  std::uint32_t ambientOcclusion{};
  double sourceDepthNumerator{};
  double sourceDepthDenominatorConstant{};
  Vec3 sourceDepthDenominatorGradient{};
  bool hasSourceDepth{};
  Vec3 sourceTextureOffset{};
  Vec3 sourceTextureForward{};
  Vec3 sourceTextureImageX{};
  Vec3 sourceTextureImageY{};
  Vec3 sourceTextureImageZ{};
  bool hasSourceTexture{};
};

struct AveragedVolumeImage {
  int width{};
  int height{};
  std::vector<std::uint8_t> rgba;
};

inline constexpr int maximumVolumeImageWidth = 160;
inline constexpr int maximumSliceImageWidth = 320;
inline constexpr int maximumProjectedBoundaryVolumes = 8192;

[[nodiscard]] int boundaryBrightnessPercent4D(int axis, int side);
void setProjectedSourcePlane4D(ProjectedBoundaryVolume4D &volume,
                               const Camera4D &camera, int axis,
                               double planeCoordinate);

[[nodiscard]] std::vector<ProjectedBoundaryVolume4D>
projectBoundaryVolumes4D(std::span<const VisibleBoundaryCell4D> cells,
                         const Camera4D &camera,
                         bool includeSourceTexture = false);
[[nodiscard]] AveragedVolumeImage
renderAveragedVolume4D(std::span<const ProjectedBoundaryVolume4D> volumes,
                       const VolumeDisplayCamera &camera, int displayWidth,
                       int displayHeight,
                       int maximumWidth = maximumVolumeImageWidth,
                       BlockId immersionMaterial = BlockId::air,
                       VolumeViewMode viewMode = VolumeViewMode::Solid);

} // namespace proj4d
