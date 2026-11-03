#pragma once

#include <cstdint>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/math.hpp"
#include "proj4d/volume4d.hpp"

namespace proj4d {

inline constexpr double cloudCellSize = 8.0;
inline constexpr double cloudAltitude = 32.0;
inline constexpr int cloudViewRadius = 4;

struct Cloud4D {
  Vec4 minimum{};
  double size{cloudCellSize};
};

struct Cloud3D {
  Vec3 minimum{};
  double size{cloudCellSize};
};

struct Cloud2D {
  Vec2 minimum{};
  double size{cloudCellSize};
};

[[nodiscard]] bool cloudCellAt(int x, int z, int w,
                               std::uint32_t seed = 0x434C4F55U);
[[nodiscard]] std::vector<Cloud4D>
visibleClouds4D(const Vec4 &position, int radius = cloudViewRadius);
[[nodiscard]] std::vector<Cloud3D>
visibleClouds3D(const Vec3 &position, int radius = cloudViewRadius);
[[nodiscard]] std::vector<Cloud2D>
visibleClouds2D(const Vec2 &position, int radius = cloudViewRadius);
[[nodiscard]] std::vector<ProjectedBoundaryVolume4D>
projectCloudVolumes4D(const std::vector<Cloud4D> &clouds,
                      const Camera4D &camera);

} // namespace proj4d
