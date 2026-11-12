#include "proj4d/render2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "proj4d/texture_atlas.hpp"

namespace proj4d {

namespace {

bool loadedOccluder(const BlockWorld2D &world, const BlockCoord2D &coordinate) {
  const std::optional<BlockId> material = world.loadedBlockAt(coordinate);
  return !material || isSolid(*material);
}

std::uint8_t endpointOcclusion(const BlockWorld2D &world,
                               const BlockCoord2D &block, int normalAxis,
                               int normalDirection, int tangentDirection) {
  const int tangentAxis = 1 - normalAxis;
  BlockCoord2D corner = block;
  corner[static_cast<std::size_t>(normalAxis)] += normalDirection;
  corner[static_cast<std::size_t>(tangentAxis)] += tangentDirection;
  return static_cast<std::uint8_t>(loadedOccluder(world, corner));
}

std::array<std::uint8_t, 3> shade(TexturePixel pixel, double occlusion) {
  const double brightness = std::clamp(1.0 - occlusion * 0.30, 0.70, 1.0);
  return {
      static_cast<std::uint8_t>(std::lround(pixel.red * brightness)),
      static_cast<std::uint8_t>(std::lround(pixel.green * brightness)),
      static_cast<std::uint8_t>(std::lround(pixel.blue * brightness)),
  };
}

} // namespace

std::vector<VisibleEdge2D> buildVisibleEdges2DChunk(const BlockWorld2D &world,
                                                    const ChunkCoord &chunk) {
  std::vector<VisibleEdge2D> edges;
  for (int y = 0; y < chunkSize; ++y) {
    for (int x = 0; x < chunkSize; ++x) {
      const BlockCoord2D block{chunk.x * chunkSize + x,
                               chunk.y * chunkSize + y};
      const BlockId material = world.blockAt(block);
      const bool solid = isSolid(material);
      const bool fluid = isFluid(material);
      if (!solid && !fluid) {
        continue;
      }
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
          const bool boundaryHidden =
              !neighborBlock ||
              (solid ? isSolid(*neighborBlock)
                     : *neighborBlock == material || isSolid(*neighborBlock));
          if (boundaryHidden) {
            continue;
          }
          Vec2 from{static_cast<double>(block.x), static_cast<double>(block.y)};
          from[static_cast<std::size_t>(normalAxis)] +=
              direction > 0 ? 1.0 : 0.0;
          Vec2 to = from;
          to[static_cast<std::size_t>(tangentAxis)] += 1.0;
          edges.push_back(
              {block,
               tangentAxis,
               from,
               to,
               material,
               {endpointOcclusion(world, block, normalAxis, direction, -1),
                endpointOcclusion(world, block, normalAxis, direction, 1)}});
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
  struct Fragment {
    const VisibleEdge2D *edge{};
    double depth{};
    double textureAmount{};
    int sample{};
  };
  std::vector<Fragment> fragments;
  fragments.reserve(edges.size() * 4U);
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
      const double clippedAmount = (projectedAmount / toDepth) / inverseDepth;
      const double textureAmount =
          minimumAmount + clippedAmount * (maximumAmount - minimumAmount);
      fragments.push_back({&edge, depth, textureAmount, index});
    }
  }
  std::ranges::sort(fragments, [](const Fragment &left, const Fragment &right) {
    return left.sample < right.sample ||
           (left.sample == right.sample && left.depth > right.depth);
  });
  std::size_t firstFragment = 0U;
  while (firstFragment < fragments.size()) {
    std::size_t lastFragment = firstFragment + 1U;
    while (lastFragment < fragments.size() &&
           fragments[lastFragment].sample == fragments[firstFragment].sample) {
      ++lastFragment;
    }
    const std::size_t index =
        static_cast<std::size_t>(fragments[firstFragment].sample);
    std::array<int, 3> color{skyColor.red, skyColor.green, skyColor.blue};
    VisionSample2D &sample = samples[index];
    sample.solid = true;
    sample.distance = std::numeric_limits<double>::infinity();
    for (std::size_t fragmentIndex = firstFragment;
         fragmentIndex < lastFragment; ++fragmentIndex) {
      const Fragment &fragment = fragments[fragmentIndex];
      const VisibleEdge2D &edge = *fragment.edge;
      const bool targeted = targetedBlock && edge.block == *targetedBlock;
      const double occlusion =
          edge.ambientOcclusion[0] * (1.0 - fragment.textureAmount) +
          edge.ambientOcclusion[1] * fragment.textureAmount;
      const TexturePixel texture = sampleMaterialTexture1D(
          edge.material, fragment.textureAmount, edge.worldAxis);
      const std::array<std::uint8_t, 3> source =
          targeted ? std::array<std::uint8_t, 3>{255, 255, 255}
                   : shade(texture, occlusion);
      const int alpha =
          targeted
              ? 255
              : (static_cast<int>(blockOpacity(edge.material)) * texture.alpha +
                 127) /
                    255;
      for (std::size_t channel = 0; channel < color.size(); ++channel) {
        color[channel] = (static_cast<int>(source[channel]) * alpha +
                          color[channel] * (255 - alpha) + 127) /
                         255;
      }
      sample.targeted = sample.targeted || targeted;
      if (fragment.depth < sample.distance) {
        sample.worldAxis = edge.worldAxis;
        sample.distance = fragment.depth;
        sample.block = edge.block;
        sample.material = edge.material;
      }
    }
    for (std::size_t channel = 0; channel < color.size(); ++channel) {
      sample.displayColor[channel] = static_cast<std::uint8_t>(color[channel]);
    }
    firstFragment = lastFragment;
  }
  return samples;
}

int blockBrightnessPercent2D(const BlockCoord2D &block) {
  static_cast<void>(block);
  return 100;
}

std::array<std::uint8_t, 3> visionBlockColor2D(int worldAxis,
                                               const BlockCoord2D &block) {
  const auto &baseColor =
      visionAxisColors2D.at(static_cast<std::size_t>(worldAxis));
  static_cast<void>(block);
  return baseColor;
}

std::array<std::uint8_t, 3> visionBlockColor2D(BlockId material,
                                               const BlockCoord2D &block) {
  static_cast<void>(block);
  return blockColor(material).array();
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
    const BlockId material = world.blockAt(hit->block);
    const bool targeted = targetedBlock && hit->block == *targetedBlock;
    const Vec2 direction = camera.rayDirectionAt(imagePosition);
    const Vec2 point = camera.position + direction * hit->distance;
    const double textureAmount =
        point[static_cast<std::size_t>(edgeAxis)] -
        std::floor(point[static_cast<std::size_t>(edgeAxis)]);
    const TexturePixel texture =
        sampleMaterialTexture1D(material, textureAmount, edgeAxis);
    samples.push_back(
        {true, edgeAxis, hit->distance, hit->block, targeted, material,
         targeted ? std::array<std::uint8_t, 3>{255, 255, 255}
                  : std::array<std::uint8_t, 3>{texture.red, texture.green,
                                                texture.blue}});
  }
  return samples;
}

} // namespace proj4d
