#include "proj4d/render_geometry.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <map>

namespace proj4d {

namespace {

std::array<int, 3> cellAxes(int boundaryAxis) {
  std::array<int, 3> axes{};
  int output = 0;
  for (int axis = 0; axis < 4; ++axis) {
    if (axis != boundaryAxis) {
      axes[static_cast<std::size_t>(output++)] = axis;
    }
  }
  return axes;
}

BlockCoord cellVertex(const VisibleBoundaryCell4D &cell, int bitMask) {
  BlockCoord vertex = cell.block;
  vertex[static_cast<std::size_t>(cell.axis)] += cell.side > 0 ? 1 : 0;
  const auto axes = cellAxes(cell.axis);
  for (int bit = 0; bit < 3; ++bit) {
    if ((bitMask & (1 << bit)) != 0) {
      vertex[static_cast<std::size_t>(axes[static_cast<std::size_t>(bit)])] +=
          1;
    }
  }
  return vertex;
}

Vec4 toVec4(const BlockCoord &coordinate) {
  return {
      static_cast<double>(coordinate.x),
      static_cast<double>(coordinate.y),
      static_cast<double>(coordinate.z),
      static_cast<double>(coordinate.w),
  };
}

struct EdgeKey {
  BlockCoord lower{};
  int axis{};

  [[nodiscard]] auto operator<=>(const EdgeKey &) const = default;
};

struct EdgeIncidence {
  std::bitset<8> boundaryNormals{};
};

void appendFlatSurfaceGuide(std::vector<FeatureEdge4D> &edges,
                            const BlockWorld &world, const BlockCoord &center,
                            int radius) {
  if (world.terrainMode() != TerrainMode::Flat) {
    return;
  }
  constexpr int surfaceY = flatGroundSurfaceY;
  if (center.y - radius > surfaceY || center.y + radius < surfaceY) {
    return;
  }

  const BlockCoord minimum{
      center.x - radius,
      surfaceY + 1,
      center.z - radius,
      center.w - radius,
  };
  const BlockCoord maximum{
      center.x + radius + 1,
      surfaceY + 1,
      center.z + radius + 1,
      center.w + radius + 1,
  };
  constexpr std::array<int, 3> horizontalAxes{0, 2, 3};
  for (const int edgeAxis : horizontalAxes) {
    std::array<int, 2> fixedAxes{};
    int fixedIndex = 0;
    for (const int axis : horizontalAxes) {
      if (axis != edgeAxis) {
        fixedAxes[static_cast<std::size_t>(fixedIndex++)] = axis;
      }
    }
    for (int corner = 0; corner < 4; ++corner) {
      BlockCoord from = minimum;
      for (int bit = 0; bit < 2; ++bit) {
        if ((corner & (1 << bit)) != 0) {
          const std::size_t axis = static_cast<std::size_t>(
              fixedAxes[static_cast<std::size_t>(bit)]);
          from[axis] = maximum[axis];
        }
      }
      BlockCoord to = from;
      to[static_cast<std::size_t>(edgeAxis)] =
          maximum[static_cast<std::size_t>(edgeAxis)];
      edges.push_back({toVec4(from), toVec4(to), 1});
    }
  }
}

Vec4 edgeMidpoint(const FeatureEdge4D &edge) {
  return (edge.from + edge.to) * 0.5;
}

bool hasClearSightline(const BlockWorld &world, const Camera4D &camera,
                       const FeatureEdge4D &edge) {
  constexpr double targetInset = 1.0e-5;
  const Vec4 offset = edgeMidpoint(edge) - camera.position;
  const double distance = length(offset);
  if (distance <= camera.nearPlane) {
    return true;
  }
  const auto obstruction =
      raycast(world, camera.position, offset, distance - targetInset);
  return !obstruction;
}

std::optional<Line3> projectEdge(const Camera4D &camera, Vec4 from, Vec4 to,
                                 int boundaryAxis) {
  double fromDepth = dot(from - camera.position, camera.forward);
  double toDepth = dot(to - camera.position, camera.forward);
  if ((fromDepth > camera.farPlane && toDepth > camera.farPlane) ||
      (fromDepth < camera.nearPlane && toDepth < camera.nearPlane)) {
    return std::nullopt;
  }
  if (fromDepth < camera.nearPlane || toDepth < camera.nearPlane) {
    const double amount =
        (camera.nearPlane - fromDepth) / (toDepth - fromDepth);
    const Vec4 clipped = lerp(from, to, amount);
    if (fromDepth < camera.nearPlane) {
      from = clipped;
    } else {
      to = clipped;
    }
  }
  const auto projectedFrom = camera.project(from);
  const auto projectedTo = camera.project(to);
  if (!projectedFrom || !projectedTo) {
    return std::nullopt;
  }
  return clipLineToVisionCube(
      {projectedFrom->position, projectedTo->position, boundaryAxis});
}

} // namespace

std::vector<FeatureEdge4D> buildFeatureEdges(const BlockWorld &world,
                                             const BlockCoord &center,
                                             int radius) {
  std::map<EdgeKey, EdgeIncidence> incidences;
  const std::vector<VisibleBoundaryCell4D> visibleCells =
      buildVisibleBoundaryCells(world, center, radius);
  for (const VisibleBoundaryCell4D &cell : visibleCells) {
    const auto axes = cellAxes(cell.axis);
    const std::size_t normalIndex =
        static_cast<std::size_t>(cell.axis * 2 + (cell.side > 0 ? 1 : 0));
    for (int vertex = 0; vertex < 8; ++vertex) {
      for (int bit = 0; bit < 3; ++bit) {
        if ((vertex & (1 << bit)) != 0) {
          continue;
        }
        const BlockCoord from = cellVertex(cell, vertex);
        const int edgeAxis = axes[static_cast<std::size_t>(bit)];
        incidences[{from, edgeAxis}].boundaryNormals.set(normalIndex);
      }
    }
  }

  std::vector<FeatureEdge4D> edges;
  edges.reserve(incidences.size());
  for (const auto &[key, incidence] : incidences) {
    std::bitset<4> boundaryAxes;
    for (std::size_t normal = 0; normal < incidence.boundaryNormals.size();
         ++normal) {
      if (incidence.boundaryNormals.test(normal)) {
        boundaryAxes.set(normal / 2U);
      }
    }
    if (boundaryAxes.count() < 3U) {
      continue;
    }
    BlockCoord upper = key.lower;
    upper[static_cast<std::size_t>(key.axis)] += 1;
    int representativeAxis = 0;
    for (std::size_t normal = 0; normal < incidence.boundaryNormals.size();
         ++normal) {
      if (incidence.boundaryNormals.test(normal)) {
        representativeAxis = static_cast<int>(normal / 2U);
        break;
      }
    }
    edges.push_back({toVec4(key.lower), toVec4(upper), representativeAxis});
  }
  appendFlatSurfaceGuide(edges, world, center, radius);
  return edges;
}

std::vector<VisibleBoundaryCell4D>
buildVisibleBoundaryCells(const BlockWorld &world, const BlockCoord &center,
                          int radius) {
  if (radius < 0) {
    return {};
  }
  std::vector<VisibleBoundaryCell4D> visibleCells;
  for (int w = center.w - radius; w <= center.w + radius; ++w) {
    for (int z = center.z - radius; z <= center.z + radius; ++z) {
      for (int y = center.y - radius; y <= center.y + radius; ++y) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
          const BlockCoord block{x, y, z, w};
          if (!world.isSolid(block)) {
            continue;
          }
          for (int axis = 0; axis < 4; ++axis) {
            for (const int side : {-1, 1}) {
              BlockCoord neighbor = block;
              neighbor[static_cast<std::size_t>(axis)] += side;
              if (!world.isSolid(neighbor)) {
                visibleCells.push_back({block, axis, side});
              }
            }
          }
        }
      }
    }
  }
  return visibleCells;
}

std::optional<Line3> clipLineToVisionCube(Line3 line) {
  double minimumAmount = 0.0;
  double maximumAmount = 1.0;
  const Vec3 delta = line.to - line.from;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const double start = line.from[axis];
    const double change = delta[axis];
    if (std::abs(change) <= 1.0e-12) {
      if (start < -1.0 || start > 1.0) {
        return std::nullopt;
      }
      continue;
    }
    double first = (-1.0 - start) / change;
    double second = (1.0 - start) / change;
    if (first > second) {
      std::swap(first, second);
    }
    minimumAmount = std::max(minimumAmount, first);
    maximumAmount = std::min(maximumAmount, second);
    if (minimumAmount > maximumAmount) {
      return std::nullopt;
    }
  }
  const Vec3 originalFrom = line.from;
  line.from = originalFrom + delta * minimumAmount;
  line.to = originalFrom + delta * maximumAmount;
  return line;
}

std::vector<Line3> projectFeatureEdges(std::span<const FeatureEdge4D> edges,
                                       const Camera4D &camera) {
  std::vector<Line3> lines;
  lines.reserve(edges.size());
  for (const FeatureEdge4D &edge : edges) {
    if (auto line = projectEdge(camera, edge.from, edge.to,
                                edge.representativeBoundaryAxis)) {
      lines.push_back(*line);
    }
  }
  return lines;
}

std::vector<Line3>
projectVisibleFeatureEdges(const BlockWorld &world,
                           std::span<const FeatureEdge4D> edges,
                           const Camera4D &camera) {
  std::vector<Line3> lines;
  lines.reserve(edges.size());
  for (const FeatureEdge4D &edge : edges) {
    const auto line = projectEdge(camera, edge.from, edge.to,
                                  edge.representativeBoundaryAxis);
    if (line && hasClearSightline(world, camera, edge)) {
      lines.push_back(*line);
    }
  }
  return lines;
}

std::vector<Line3> buildVisionGeometry(const BlockWorld &world,
                                       const Camera4D &camera,
                                       const BlockCoord &center, int radius) {
  return projectVisibleFeatureEdges(
      world, buildFeatureEdges(world, center, radius), camera);
}

std::vector<Line3> buildTesseractWireframe(const BlockCoord &block,
                                           const Camera4D &camera) {
  std::vector<Line3> lines;
  lines.reserve(32U);
  for (int vertexMask = 0; vertexMask < 16; ++vertexMask) {
    Vec4 from{
        static_cast<double>(block.x + ((vertexMask & 1) != 0 ? 1 : 0)),
        static_cast<double>(block.y + ((vertexMask & 2) != 0 ? 1 : 0)),
        static_cast<double>(block.z + ((vertexMask & 4) != 0 ? 1 : 0)),
        static_cast<double>(block.w + ((vertexMask & 8) != 0 ? 1 : 0)),
    };
    for (int axis = 0; axis < 4; ++axis) {
      if ((vertexMask & (1 << axis)) != 0) {
        continue;
      }
      Vec4 to = from;
      to[static_cast<std::size_t>(axis)] += 1.0;
      if (auto line = projectEdge(camera, from, to, axis)) {
        lines.push_back(*line);
      }
    }
  }
  return lines;
}

} // namespace proj4d
