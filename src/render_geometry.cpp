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
      edges.push_back({toVec4(from), toVec4(to), edgeAxis});
    }
  }
}

bool hasClearSightline(const BlockWorld &world, const Camera4D &camera,
                       const Vec4 &point) {
  constexpr double targetInset = 1.0e-5;
  const Vec4 offset = point - camera.position;
  const double distance = length(offset);
  if (distance <= camera.nearPlane) {
    return true;
  }
  const auto obstruction =
      raycast(world, camera.position, offset, distance - targetInset);
  return !obstruction;
}

struct ClippedLine3 {
  Line3 line{};
  double minimumAmount{};
  double maximumAmount{1.0};
};

std::optional<ClippedLine3> clipLineToVisionCubeWithAmounts(Line3 line) {
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
  return ClippedLine3{line, minimumAmount, maximumAmount};
}

double perspectiveParameter(double projectedAmount, double fromDepth,
                            double toDepth) {
  const double weightedFrom = (1.0 - projectedAmount) / fromDepth;
  const double weightedTo = projectedAmount / toDepth;
  return weightedTo / (weightedFrom + weightedTo);
}

struct ProjectedSourceEdge {
  Vec4 sourceFrom{};
  Vec4 sourceTo{};
  double fromDepth{};
  double toDepth{};
  Line3 line{};
};

std::optional<ProjectedSourceEdge>
projectSourceEdge(const Camera4D &camera, const FeatureEdge4D &edge) {
  constexpr double clippingMargin = 1.0e-7;
  const Vec4 originalFrom = edge.from;
  const Vec4 originalTo = edge.to;
  const double originalFromDepth =
      dot(originalFrom - camera.position, camera.forward);
  const double originalToDepth =
      dot(originalTo - camera.position, camera.forward);
  const double depthChange = originalToDepth - originalFromDepth;
  double minimumAmount = 0.0;
  double maximumAmount = 1.0;
  if (std::abs(depthChange) <= 1.0e-12) {
    if (originalFromDepth < camera.nearPlane ||
        originalFromDepth > camera.farPlane) {
      return std::nullopt;
    }
  } else {
    double nearAmount =
        (camera.nearPlane + clippingMargin - originalFromDepth) / depthChange;
    double farAmount =
        (camera.farPlane - clippingMargin - originalFromDepth) / depthChange;
    if (nearAmount > farAmount) {
      std::swap(nearAmount, farAmount);
    }
    minimumAmount = std::max(minimumAmount, nearAmount);
    maximumAmount = std::min(maximumAmount, farAmount);
    if (minimumAmount > maximumAmount) {
      return std::nullopt;
    }
  }

  const Vec4 depthClippedFrom = lerp(originalFrom, originalTo, minimumAmount);
  const Vec4 depthClippedTo = lerp(originalFrom, originalTo, maximumAmount);
  const double fromDepth =
      dot(depthClippedFrom - camera.position, camera.forward);
  const double toDepth = dot(depthClippedTo - camera.position, camera.forward);
  const auto projectedFrom = camera.project(depthClippedFrom);
  const auto projectedTo = camera.project(depthClippedTo);
  if (!projectedFrom || !projectedTo) {
    return std::nullopt;
  }
  const auto clipped = clipLineToVisionCubeWithAmounts(
      {projectedFrom->position, projectedTo->position, edge.worldAxis});
  if (!clipped) {
    return std::nullopt;
  }

  const double sourceMinimum =
      perspectiveParameter(clipped->minimumAmount, fromDepth, toDepth);
  const double sourceMaximum =
      perspectiveParameter(clipped->maximumAmount, fromDepth, toDepth);
  const Vec4 sourceFrom = lerp(depthClippedFrom, depthClippedTo, sourceMinimum);
  const Vec4 sourceTo = lerp(depthClippedFrom, depthClippedTo, sourceMaximum);
  return ProjectedSourceEdge{
      sourceFrom,
      sourceTo,
      dot(sourceFrom - camera.position, camera.forward),
      dot(sourceTo - camera.position, camera.forward),
      clipped->line,
  };
}

void appendVisibleFragments(const BlockWorld &world, const Camera4D &camera,
                            const ProjectedSourceEdge &edge,
                            std::vector<Line3> &lines) {
  const Vec3 projectedDelta = edge.line.to - edge.line.from;
  const int sampleCount =
      std::clamp(static_cast<int>(std::ceil(length(projectedDelta) *
                                            visibilitySamplesPerVisionUnit)),
                 1, maximumVisibilitySamplesPerEdge);
  std::optional<double> visibleRunStart;
  for (int sample = 0; sample < sampleCount; ++sample) {
    const double intervalStart =
        static_cast<double>(sample) / static_cast<double>(sampleCount);
    const double intervalEnd =
        static_cast<double>(sample + 1) / static_cast<double>(sampleCount);
    const double projectedAmount = (intervalStart + intervalEnd) * 0.5;
    const double sourceAmount =
        perspectiveParameter(projectedAmount, edge.fromDepth, edge.toDepth);
    const Vec4 sourcePoint = lerp(edge.sourceFrom, edge.sourceTo, sourceAmount);
    const bool visible = hasClearSightline(world, camera, sourcePoint);
    if (visible && !visibleRunStart) {
      visibleRunStart = intervalStart;
    }
    if (!visible && visibleRunStart) {
      lines.push_back({edge.line.from + projectedDelta * *visibleRunStart,
                       edge.line.from + projectedDelta * intervalStart,
                       edge.line.worldAxis});
      visibleRunStart.reset();
    }
  }
  if (visibleRunStart) {
    lines.push_back({edge.line.from + projectedDelta * *visibleRunStart,
                     edge.line.to, edge.line.worldAxis});
  }
}

std::vector<FeatureEdge4D> tesseractEdges(const BlockCoord &block) {
  std::vector<FeatureEdge4D> edges;
  edges.reserve(32U);
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
      edges.push_back({from, to, axis});
    }
  }
  return edges;
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
    edges.push_back({toVec4(key.lower), toVec4(upper), key.axis});
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
  const auto clipped = clipLineToVisionCubeWithAmounts(line);
  return clipped ? std::optional(clipped->line) : std::nullopt;
}

std::vector<Line3> projectFeatureEdges(std::span<const FeatureEdge4D> edges,
                                       const Camera4D &camera) {
  std::vector<Line3> lines;
  lines.reserve(edges.size());
  for (const FeatureEdge4D &edge : edges) {
    if (const auto projected = projectSourceEdge(camera, edge)) {
      lines.push_back(projected->line);
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
    if (const auto projected = projectSourceEdge(camera, edge)) {
      appendVisibleFragments(world, camera, *projected, lines);
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
  return projectFeatureEdges(tesseractEdges(block), camera);
}

std::vector<Line3> buildVisibleTesseractWireframe(const BlockWorld &world,
                                                  const BlockCoord &block,
                                                  const Camera4D &camera) {
  return projectVisibleFeatureEdges(world, tesseractEdges(block), camera);
}

} // namespace proj4d
