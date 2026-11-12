#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

struct Line3 {
  Vec3 from{};
  Vec3 to{};
  int worldAxis{};
  BlockId material{BlockId::stone};
};

struct FeatureEdge4D {
  Vec4 from{};
  Vec4 to{};
  int worldAxis{};
  BlockId material{BlockId::stone};
};

struct VisibleBoundaryCell4D {
  BlockCoord block{};
  int axis{};
  int side{};
  BlockId material{BlockId::stone};
  std::uint32_t ambientOcclusion{};
};

[[nodiscard]] constexpr std::uint8_t ambientOcclusionAt4D(std::uint32_t packed,
                                                          int corner) {
  return static_cast<std::uint8_t>((packed >> (std::clamp(corner, 0, 7) * 3)) &
                                   0x7U);
}

[[nodiscard]] int averageAmbientOcclusionPercent4D(std::uint32_t packed);

class VisibilityGrid4D {
public:
  VisibilityGrid4D() = default;
  VisibilityGrid4D(BlockCoord minimum, int extent, std::vector<BlockId> blocks);

  [[nodiscard]] bool contains(const BlockCoord &block) const;
  [[nodiscard]] BlockId blockAt(const BlockCoord &block) const;
  [[nodiscard]] bool isSolid(const BlockCoord &block) const;
  [[nodiscard]] const BlockCoord &minimum() const;
  [[nodiscard]] int extent() const;

private:
  BlockCoord minimum_{};
  int extent_{};
  std::vector<BlockId> blocks_;
};

inline constexpr int renderBlockRadius = 6;
inline constexpr double visibilitySamplesPerVisionUnit = 96.0;
inline constexpr int maximumVisibilitySamplesPerEdge = 64;

[[nodiscard]] VisibilityGrid4D
buildVisibilityGrid4D(const BlockWorld &world, const BlockCoord &center,
                      int radius = renderBlockRadius,
                      const VisibilityGrid4D *previous = nullptr);

[[nodiscard]] std::vector<VisibleBoundaryCell4D>
buildVisibleBoundaryCells(const BlockWorld &world, const BlockCoord &center,
                          int radius = renderBlockRadius);
[[nodiscard]] std::vector<VisibleBoundaryCell4D>
buildVisibleBoundaryCells(const VisibilityGrid4D &grid,
                          const BlockCoord &center,
                          int radius = renderBlockRadius);
[[nodiscard]] std::vector<FeatureEdge4D>
buildFeatureEdges(const BlockWorld &world, const BlockCoord &center,
                  int radius = renderBlockRadius);
[[nodiscard]] std::vector<FeatureEdge4D>
buildFeatureEdges(std::span<const VisibleBoundaryCell4D> cells);
[[nodiscard]] std::optional<Line3> clipLineToVisionCube(Line3 line);
[[nodiscard]] std::vector<Line3>
projectFeatureEdges(std::span<const FeatureEdge4D> edges,
                    const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
projectVisibleFeatureEdges(const BlockWorld &world,
                           std::span<const FeatureEdge4D> edges,
                           const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
projectVisibleFeatureEdges(const VisibilityGrid4D &grid,
                           std::span<const FeatureEdge4D> edges,
                           const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
buildVisionGeometry(const BlockWorld &world, const Camera4D &camera,
                    const BlockCoord &center, int radius = renderBlockRadius);
[[nodiscard]] std::vector<Line3>
buildTesseractWireframe(const BlockCoord &block, const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
buildVisibleTesseractWireframe(const BlockWorld &world, const BlockCoord &block,
                               const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
buildVisibleTesseractWireframe(const VisibilityGrid4D &grid,
                               const BlockCoord &block, const Camera4D &camera);

} // namespace proj4d
