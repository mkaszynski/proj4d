#include "proj4d/render_chunks.hpp"

#include <algorithm>
#include <utility>

namespace proj4d {

namespace {

struct PlannedChunk {
  int distanceSquared{};
  ChunkCoord coordinate{};
};

std::vector<ChunkCoord> orderedCoordinates(std::vector<PlannedChunk> planned) {
  std::ranges::sort(planned,
                    [](const PlannedChunk &left, const PlannedChunk &right) {
                      return left.distanceSquared != right.distanceSquared
                                 ? left.distanceSquared < right.distanceSquared
                                 : left.coordinate < right.coordinate;
                    });
  std::vector<ChunkCoord> chunks;
  chunks.reserve(planned.size());
  for (const PlannedChunk &chunk : planned) {
    chunks.push_back(chunk.coordinate);
  }
  return chunks;
}

} // namespace

std::vector<ChunkCoord> planRenderChunks2D(const ChunkCoord &center,
                                           int radius) {
  radius = std::max(0, radius);
  std::vector<PlannedChunk> planned;
  const int radiusSquared = radius * radius;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      const int distanceSquared = x * x + y * y;
      if (distanceSquared <= radiusSquared) {
        planned.push_back(
            {distanceSquared, {center.x + x, center.y + y, 0, 0}});
      }
    }
  }
  return orderedCoordinates(std::move(planned));
}

std::vector<ChunkCoord> planRenderChunks3D(const ChunkCoord &center,
                                           int radius) {
  radius = std::max(0, radius);
  std::vector<PlannedChunk> planned;
  const int radiusSquared = radius * radius;
  for (int z = -radius; z <= radius; ++z) {
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        const int distanceSquared = x * x + y * y + z * z;
        if (distanceSquared <= radiusSquared) {
          planned.push_back(
              {distanceSquared, {center.x + x, center.y + y, center.z + z, 0}});
        }
      }
    }
  }
  return orderedCoordinates(std::move(planned));
}

} // namespace proj4d
