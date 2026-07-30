#pragma once

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
};

struct FeatureEdge4D {
  Vec4 from{};
  Vec4 to{};
  int worldAxis{};
};

struct VisibleBoundaryCell4D {
  BlockCoord block{};
  int axis{};
  int side{};
};

inline constexpr int renderBlockRadius = 6;

[[nodiscard]] std::vector<VisibleBoundaryCell4D>
buildVisibleBoundaryCells(const BlockWorld &world, const BlockCoord &center,
                          int radius = renderBlockRadius);
[[nodiscard]] std::vector<FeatureEdge4D>
buildFeatureEdges(const BlockWorld &world, const BlockCoord &center,
                  int radius = renderBlockRadius);
[[nodiscard]] std::optional<Line3> clipLineToVisionCube(Line3 line);
[[nodiscard]] std::vector<Line3>
projectFeatureEdges(std::span<const FeatureEdge4D> edges,
                    const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
projectVisibleFeatureEdges(const BlockWorld &world,
                           std::span<const FeatureEdge4D> edges,
                           const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
buildVisionGeometry(const BlockWorld &world, const Camera4D &camera,
                    const BlockCoord &center, int radius = renderBlockRadius);
[[nodiscard]] std::vector<Line3>
buildTesseractWireframe(const BlockCoord &block, const Camera4D &camera);

} // namespace proj4d
