#include "proj4d/render2d.hpp"

#include <algorithm>
#include <cstdint>

namespace proj4d {

int blockBrightnessPercent2D(const BlockCoord2D &block) {
  std::uint32_t hash = 2166136261U;
  hash = (hash ^ static_cast<std::uint32_t>(block.x)) * 16777619U;
  hash = (hash ^ static_cast<std::uint32_t>(block.y)) * 16777619U;
  hash ^= hash >> 16U;
  constexpr int shadeCount =
      visionMaximumBrightnessPercent2D - visionMinimumBrightnessPercent2D + 1;
  return visionMinimumBrightnessPercent2D +
         static_cast<int>(hash % static_cast<std::uint32_t>(shadeCount));
}

std::array<std::uint8_t, 3> visionBlockColor2D(int worldAxis,
                                               const BlockCoord2D &block) {
  const auto &baseColor =
      visionAxisColors2D.at(static_cast<std::size_t>(worldAxis));
  const int brightness = blockBrightnessPercent2D(block);
  std::array<std::uint8_t, 3> result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    result[channel] = static_cast<std::uint8_t>(std::clamp(
        (static_cast<int>(baseColor[channel]) * brightness + 50) / 100, 0,
        255));
  }
  return result;
}

std::vector<VisionSample2D>
buildVisionLine(const BlockWorld2D &world, const Camera2D &camera,
                int sampleCount, double maximumDistance,
                std::optional<BlockCoord2D> targetedBlock) {
  sampleCount = std::max(1, sampleCount);
  std::vector<VisionSample2D> samples;
  samples.reserve(static_cast<std::size_t>(sampleCount));
  for (int index = 0; index < sampleCount; ++index) {
    const double imagePosition =
        sampleCount == 1 ? 0.0
                         : -1.0 + 2.0 * static_cast<double>(index) /
                                      static_cast<double>(sampleCount - 1);
    const auto hit =
        raycast(world, camera.position, camera.rayDirectionAt(imagePosition),
                maximumDistance);
    if (!hit) {
      samples.push_back({});
      continue;
    }
    // Crossing an X boundary exposes an edge running along Y, and vice versa.
    const int edgeAxis = hit->boundaryNormalAxis == 0 ? 1 : 0;
    samples.push_back({true, edgeAxis, hit->distance, hit->block,
                       targetedBlock && hit->block == *targetedBlock});
  }
  return samples;
}

} // namespace proj4d
