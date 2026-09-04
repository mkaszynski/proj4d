#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "proj4d/camera2d.hpp"
#include "proj4d/world2d.hpp"

namespace proj4d {

inline constexpr int visionLineWidthDivisor = 10;
inline constexpr std::array<std::array<std::uint8_t, 3>, 2> visionAxisColors2D{{
    {150, 245, 165},
    {245, 140, 140},
}};

struct VisionSample2D {
  bool solid{};
  int worldAxis{};
  double distance{};
  BlockCoord2D block{};
  bool targeted{};
};

[[nodiscard]] std::vector<VisionSample2D>
buildVisionLine(const BlockWorld2D &world, const Camera2D &camera,
                int sampleCount, double maximumDistance,
                std::optional<BlockCoord2D> targetedBlock = std::nullopt);

} // namespace proj4d
