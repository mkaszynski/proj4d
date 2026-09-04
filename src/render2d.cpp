#include "proj4d/render2d.hpp"

#include <algorithm>

namespace proj4d {

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
