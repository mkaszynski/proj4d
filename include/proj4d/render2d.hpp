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
inline constexpr int visionMinimumBrightnessPercent2D = 82;
inline constexpr int visionMaximumBrightnessPercent2D = 118;

[[nodiscard]] int blockBrightnessPercent2D(const BlockCoord2D &block);
[[nodiscard]] std::array<std::uint8_t, 3>
visionBlockColor2D(int worldAxis, const BlockCoord2D &block);

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
