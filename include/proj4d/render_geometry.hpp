#pragma once

#include <optional>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

struct Line3 {
  Vec3 from{};
  Vec3 to{};
  int boundaryAxis{};
};

struct FeatureEdge4D {
  Vec4 from{};
  Vec4 to{};
  int representativeBoundaryAxis{};
};

[[nodiscard]] std::vector<FeatureEdge4D>
buildFeatureEdges(const BlockWorld &world);
[[nodiscard]] std::optional<Line3> clipLineToVisionCube(Line3 line);
[[nodiscard]] std::vector<Line3> buildVisionGeometry(const BlockWorld &world,
                                                     const Camera4D &camera);
[[nodiscard]] std::vector<Line3>
buildTesseractWireframe(const BlockCoord &block, const Camera4D &camera);

} // namespace proj4d
