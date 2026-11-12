#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "proj4d/camera2d.hpp"
#include "proj4d/world2d.hpp"

namespace proj4d {

inline constexpr int visionLineWidthDivisor = 10;
inline constexpr int terrainRenderDistance2D = 24;
inline constexpr std::array<std::array<std::uint8_t, 3>, 2> visionAxisColors2D{{
    {150, 245, 165},
    {245, 140, 140},
}};
[[nodiscard]] int blockBrightnessPercent2D(const BlockCoord2D &block);
[[nodiscard]] std::array<std::uint8_t, 3>
visionBlockColor2D(int worldAxis, const BlockCoord2D &block);
[[nodiscard]] std::array<std::uint8_t, 3>
visionBlockColor2D(BlockId material, const BlockCoord2D &block);

struct VisionSample2D {
  bool solid{};
  int worldAxis{};
  double distance{};
  BlockCoord2D block{};
  bool targeted{};
  BlockId material{BlockId::air};
  std::array<std::uint8_t, 3> displayColor{};
};

struct VisibleEdge2D {
  BlockCoord2D block{};
  int worldAxis{};
  Vec2 from{};
  Vec2 to{};
  BlockId material{BlockId::air};
  std::array<std::uint8_t, 2> ambientOcclusion{};
};

[[nodiscard]] std::vector<VisibleEdge2D>
buildVisibleEdges2DChunk(const BlockWorld2D &world, const ChunkCoord &chunk);
[[nodiscard]] std::vector<VisionSample2D> rasterizeVisionEdges2D(
    std::span<const VisibleEdge2D> edges, const Camera2D &camera,
    int sampleCount, std::optional<BlockCoord2D> targetedBlock = std::nullopt);

[[nodiscard]] std::vector<VisionSample2D>
buildVisionLine(const BlockWorld2D &world, const Camera2D &camera,
                int sampleCount, double maximumDistance,
                std::optional<BlockCoord2D> targetedBlock = std::nullopt);

} // namespace proj4d
