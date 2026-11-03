#include "proj4d/render2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace proj4d {

std::vector<VisibleEdge2D> buildVisibleEdges2DChunk(const BlockWorld2D &world,
                                                    const ChunkCoord &chunk) {
  std::vector<VisibleEdge2D> edges;
  for (int y = 0; y < chunkSize; ++y) {
    for (int x = 0; x < chunkSize; ++x) {
      const BlockCoord2D block{chunk.x * chunkSize + x,
                               chunk.y * chunkSize + y};
      if (!world.isSolid(block)) {
        continue;
      }
      const BlockId material = world.blockAt(block);
      for (int normalAxis = 0; normalAxis < 2; ++normalAxis) {
        const int tangentAxis = 1 - normalAxis;
        for (const int direction : {-1, 1}) {
          BlockCoord2D neighbor = block;
          neighbor[static_cast<std::size_t>(normalAxis)] += direction;
          const std::optional<BlockId> neighborBlock =
              world.loadedBlockAt(neighbor);
          // Chunk geometry must never synchronously generate an unseen
          // neighbor. Treat missing data as opaque until that chunk enters
          // the visible set and the cache is rebuilt.
          if (!neighborBlock || isSolid(*neighborBlock)) {
            continue;
          }
          Vec2 from{static_cast<double>(block.x), static_cast<double>(block.y)};
          from[static_cast<std::size_t>(normalAxis)] +=
              direction > 0 ? 1.0 : 0.0;
          Vec2 to = from;
          to[static_cast<std::size_t>(tangentAxis)] += 1.0;
          edges.push_back({block, tangentAxis, from, to, material});
        }
      }
    }
  }
  return edges;
}

std::vector<VisionSample2D>
rasterizeVisionEdges2D(std::span<const VisibleEdge2D> edges,
                       const Camera2D &camera, int sampleCount,
                       std::optional<BlockCoord2D> targetedBlock) {
  sampleCount = std::max(1, sampleCount);
  std::vector<VisionSample2D> samples(static_cast<std::size_t>(sampleCount));
  for (VisionSample2D &sample : samples) {
    sample.distance = std::numeric_limits<double>::infinity();
  }
  const Vec2 forward = camera.forward();
  constexpr double clippingMargin = 1.0e-7;
  for (const VisibleEdge2D &edge : edges) {
    Vec2 from = edge.from;
    Vec2 to = edge.to;
    double fromDepth = dot(from - camera.position, forward);
    double toDepth = dot(to - camera.position, forward);
    const double depthChange = toDepth - fromDepth;
    double minimumAmount = 0.0;
    double maximumAmount = 1.0;
    if (std::abs(depthChange) <= 1.0e-12) {
      if (fromDepth < camera.nearPlane || fromDepth > camera.farPlane) {
        continue;
      }
    } else {
      double nearAmount =
          (camera.nearPlane + clippingMargin - fromDepth) / depthChange;
      double farAmount =
          (camera.farPlane - clippingMargin - fromDepth) / depthChange;
      if (nearAmount > farAmount) {
        std::swap(nearAmount, farAmount);
      }
      minimumAmount = std::max(minimumAmount, nearAmount);
      maximumAmount = std::min(maximumAmount, farAmount);
      if (minimumAmount > maximumAmount) {
        continue;
      }
    }
    const Vec2 delta = to - from;
    to = from + delta * maximumAmount;
    from = from + delta * minimumAmount;
    fromDepth = dot(from - camera.position, forward);
    toDepth = dot(to - camera.position, forward);
    const auto projectedFrom = camera.project(from);
    const auto projectedTo = camera.project(to);
    if (!projectedFrom || !projectedTo) {
      continue;
    }
    const double firstPosition = projectedFrom->position;
    const double secondPosition = projectedTo->position;
    const double minimumPosition =
        std::max(-1.0, std::min(firstPosition, secondPosition));
    const double maximumPosition =
        std::min(1.0, std::max(firstPosition, secondPosition));
    if (minimumPosition > maximumPosition) {
      continue;
    }
    const auto sampleForPosition = [sampleCount](double position) {
      return (position + 1.0) * 0.5 * static_cast<double>(sampleCount - 1);
    };
    const int firstSample = std::clamp(
        static_cast<int>(std::ceil(sampleForPosition(minimumPosition))), 0,
        sampleCount - 1);
    const int lastSample = std::clamp(
        static_cast<int>(std::floor(sampleForPosition(maximumPosition))), 0,
        sampleCount - 1);
    for (int index = firstSample; index <= lastSample; ++index) {
      const double imagePosition =
          sampleCount == 1 ? 0.0
                           : -1.0 + 2.0 * static_cast<double>(index) /
                                        static_cast<double>(sampleCount - 1);
      double projectedAmount = 0.5;
      if (std::abs(secondPosition - firstPosition) > 1.0e-12) {
        projectedAmount = std::clamp((imagePosition - firstPosition) /
                                         (secondPosition - firstPosition),
                                     0.0, 1.0);
      }
      const double inverseDepth =
          (1.0 - projectedAmount) / fromDepth + projectedAmount / toDepth;
      if (inverseDepth <= 0.0) {
        continue;
      }
      const double depth = 1.0 / inverseDepth;
      VisionSample2D &sample = samples[static_cast<std::size_t>(index)];
      if (depth >= sample.distance) {
        continue;
      }
      sample = {true,
                edge.worldAxis,
                depth,
                edge.block,
                targetedBlock && edge.block == *targetedBlock,
                edge.material};
    }
  }
  return samples;
}

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

std::array<std::uint8_t, 3> visionBlockColor2D(BlockId material,
                                               const BlockCoord2D &block) {
  return shadedBlockColor(material, blockBrightnessPercent2D(block)).array();
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
                       targetedBlock && hit->block == *targetedBlock,
                       world.blockAt(hit->block)});
  }
  return samples;
}

} // namespace proj4d
