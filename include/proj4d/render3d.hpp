#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "proj4d/camera3d.hpp"
#include "proj4d/world3d.hpp"

namespace proj4d {

inline constexpr int renderBlockRadius3D = 8;
inline constexpr std::array<std::array<std::uint8_t, 3>, 3> visionFaceColors3D{{
    {225, 85, 85},
    {85, 220, 110},
    {80, 135, 235},
}};
inline constexpr int visionMinimumBrightnessPercent3D = 82;
inline constexpr int visionMaximumBrightnessPercent3D = 118;

[[nodiscard]] int blockBrightnessPercent3D(const BlockCoord3D &block);
[[nodiscard]] std::array<std::uint8_t, 3>
visionBlockColor3D(int worldAxis, const BlockCoord3D &block);

struct VisionSample3D {
  bool solid{};
  int worldAxis{};
  double distance{};
  BlockCoord3D block{};
};

struct VisibleFace3D {
  BlockCoord3D block{};
  int worldAxis{};
  int normalDirection{};
  std::array<Vec3, 4> corners{};
};

struct ProjectedFace3D {
  BlockCoord3D block{};
  int worldAxis{};
  double depth{};
  std::array<Vec2, 8> vertices{};
  std::size_t vertexCount{};
};

struct Line2D {
  Vec2 from{};
  Vec2 to{};
};

[[nodiscard]] std::vector<VisionSample3D>
buildVisionImage3D(const BlockWorld3D &world, const Camera3D &camera, int width,
                   int height, double maximumDistance);
[[nodiscard]] std::vector<VisibleFace3D>
buildVisibleFaces3D(const BlockWorld3D &world, const BlockCoord3D &center,
                    int radius = renderBlockRadius3D);
[[nodiscard]] std::vector<VisionSample3D>
rasterizeVisionFaces3D(const std::vector<VisibleFace3D> &faces,
                       const Camera3D &camera, int width, int height);
[[nodiscard]] std::vector<ProjectedFace3D>
projectVisibleFaces3D(const std::vector<VisibleFace3D> &faces,
                      const Camera3D &camera, double aspectRatio);
[[nodiscard]] std::vector<Line2D>
buildCubeSelectionWireframe3D(const BlockCoord3D &block, const Camera3D &camera,
                              double aspectRatio);

} // namespace proj4d
