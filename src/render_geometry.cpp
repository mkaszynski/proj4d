#include "proj4d/render_geometry.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace proj4d {

VisibilityGrid4D::VisibilityGrid4D(BlockCoord minimum, int extent,
                                   std::vector<BlockId> blocks)
    : minimum_(minimum), extent_(extent), blocks_(std::move(blocks)) {}

bool VisibilityGrid4D::contains(const BlockCoord &block) const {
  return extent_ > 0 && block.x >= minimum_.x && block.y >= minimum_.y &&
         block.z >= minimum_.z && block.w >= minimum_.w &&
         block.x < minimum_.x + extent_ && block.y < minimum_.y + extent_ &&
         block.z < minimum_.z + extent_ && block.w < minimum_.w + extent_;
}

BlockId VisibilityGrid4D::blockAt(const BlockCoord &block) const {
  if (!contains(block)) {
    return BlockId::stone;
  }
  const std::size_t x = static_cast<std::size_t>(block.x - minimum_.x);
  const std::size_t y = static_cast<std::size_t>(block.y - minimum_.y);
  const std::size_t z = static_cast<std::size_t>(block.z - minimum_.z);
  const std::size_t w = static_cast<std::size_t>(block.w - minimum_.w);
  const std::size_t size = static_cast<std::size_t>(extent_);
  return blocks_[x + size * (z + size * (y + size * w))];
}

bool VisibilityGrid4D::isSolid(const BlockCoord &block) const {
  return proj4d::isSolid(blockAt(block));
}

const BlockCoord &VisibilityGrid4D::minimum() const { return minimum_; }

int VisibilityGrid4D::extent() const { return extent_; }

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

std::uint32_t boundaryAmbientOcclusion(const VisibilityGrid4D &grid,
                                       const BlockCoord &block,
                                       int boundaryAxis, int boundarySide) {
  const std::array<int, 3> axes = cellAxes(boundaryAxis);
  std::array<bool, 27> occupancyMask{};
  for (int third = -1; third <= 1; ++third) {
    for (int second = -1; second <= 1; ++second) {
      for (int first = -1; first <= 1; ++first) {
        if (first == 0 && second == 0 && third == 0) {
          continue;
        }
        BlockCoord neighbor = block;
        neighbor[static_cast<std::size_t>(boundaryAxis)] += boundarySide;
        neighbor[static_cast<std::size_t>(axes[0])] += first;
        neighbor[static_cast<std::size_t>(axes[1])] += second;
        neighbor[static_cast<std::size_t>(axes[2])] += third;
        const std::size_t index = static_cast<std::size_t>(
            (first + 1) + 3 * ((second + 1) + 3 * (third + 1)));
        occupancyMask[index] = grid.isSolid(neighbor);
      }
    }
  }
  std::uint32_t packed = 0U;
  for (int corner = 0; corner < 8; ++corner) {
    std::uint8_t occupiedCount = 0U;
    for (int subset = 1; subset < 8; ++subset) {
      std::array<int, 3> offsets{};
      for (int bit = 0; bit < 3; ++bit) {
        if ((subset & (1 << bit)) == 0) {
          continue;
        }
        offsets[static_cast<std::size_t>(bit)] =
            (corner & (1 << bit)) != 0 ? 1 : -1;
      }
      const std::size_t index = static_cast<std::size_t>(
          (offsets[0] + 1) + 3 * ((offsets[1] + 1) + 3 * (offsets[2] + 1)));
      occupiedCount += static_cast<std::uint8_t>(occupancyMask[index]);
    }
    packed |= static_cast<std::uint32_t>(occupiedCount) << (corner * 3);
  }
  return packed;
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

  [[nodiscard]] bool operator==(const EdgeKey &) const = default;
};

struct EdgeKeyHash {
  [[nodiscard]] std::size_t operator()(const EdgeKey &key) const {
    const std::size_t coordinate = BlockCoordHash{}(key.lower);
    const std::size_t axis = std::hash<int>{}(key.axis);
    return coordinate ^
           (axis + 0x9e3779b9U + (coordinate << 6U) + (coordinate >> 2U));
  }
};

struct EdgeIncidence {
  std::bitset<8> boundaryNormals{};
  BlockId material{BlockId::air};
};

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

bool gridRaycastHits(const VisibilityGrid4D &grid, const Vec4 &origin,
                     const Vec4 &direction, double maximumDistance) {
  const Vec4 rayDirection = normalized(direction);
  BlockCoord current = containingBlock(origin);
  std::array<int, 4> step{};
  std::array<double, 4> nextBoundary{};
  std::array<double, 4> boundaryStride{};
  for (std::size_t axis = 0; axis < 4; ++axis) {
    const double component = rayDirection[axis];
    if (std::abs(component) <= 1.0e-12) {
      nextBoundary[axis] = std::numeric_limits<double>::infinity();
      boundaryStride[axis] = std::numeric_limits<double>::infinity();
      continue;
    }
    step[axis] = component > 0.0 ? 1 : -1;
    const double boundary = component > 0.0
                                ? static_cast<double>(current[axis] + 1)
                                : static_cast<double>(current[axis]);
    nextBoundary[axis] = (boundary - origin[axis]) / component;
    boundaryStride[axis] = std::abs(1.0 / component);
  }
  double distance = 0.0;
  while (distance <= maximumDistance) {
    if (!grid.contains(current) || grid.isSolid(current)) {
      return true;
    }
    std::size_t selectedAxis = 0;
    for (std::size_t axis = 1; axis < 4; ++axis) {
      if (nextBoundary[axis] < nextBoundary[selectedAxis]) {
        selectedAxis = axis;
      }
    }
    distance = nextBoundary[selectedAxis];
    if (!std::isfinite(distance) || distance > maximumDistance) {
      break;
    }
    current[selectedAxis] += step[selectedAxis];
    nextBoundary[selectedAxis] += boundaryStride[selectedAxis];
  }
  return false;
}

bool hasClearSightline(const VisibilityGrid4D &grid, const Camera4D &camera,
                       const Vec4 &point) {
  constexpr double targetInset = 1.0e-5;
  const Vec4 offset = point - camera.position;
  const double distance = length(offset);
  return distance <= camera.nearPlane ||
         !gridRaycastHits(grid, camera.position, offset,
                          distance - targetInset);
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
      {projectedFrom->position, projectedTo->position, edge.worldAxis,
       edge.material});
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

template <typename Occupancy>
void appendVisibleFragments(const Occupancy &world, const Camera4D &camera,
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
                       edge.line.worldAxis, edge.line.material});
      visibleRunStart.reset();
    }
  }
  if (visibleRunStart) {
    lines.push_back({edge.line.from + projectedDelta * *visibleRunStart,
                     edge.line.to, edge.line.worldAxis, edge.line.material});
  }
}

std::vector<FeatureEdge4D> tesseractEdges(const BlockCoord &block,
                                          BlockId material) {
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
      edges.push_back({from, to, axis, material});
    }
  }
  return edges;
}

} // namespace

int averageAmbientOcclusionPercent4D(std::uint32_t packed) {
  int total = 0;
  for (int corner = 0; corner < 8; ++corner) {
    total += ambientOcclusionAt4D(packed, corner);
  }
  // Seven possible tangent-space blockers per corner. Keep the darkest fully
  // enclosed contact readable while preserving a broad 55-100% range.
  return std::clamp(100 - (total * 45 + 28) / 56, 55, 100);
}

VisibilityGrid4D buildVisibilityGrid4D(const BlockWorld &world,
                                       const BlockCoord &center, int radius,
                                       const VisibilityGrid4D *previous) {
  radius = std::max(0, radius);
  const int extent = radius * 2 + 3;
  const BlockCoord minimum{center.x - radius - 1, center.y - radius - 1,
                           center.z - radius - 1, center.w - radius - 1};
  const std::size_t count =
      static_cast<std::size_t>(extent) * extent * extent * extent;
  std::vector<BlockId> blocks(count, BlockId::air);
  std::size_t index = 0U;
  for (int w = 0; w < extent; ++w) {
    for (int y = 0; y < extent; ++y) {
      for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
          const BlockCoord block{minimum.x + x, minimum.y + y, minimum.z + z,
                                 minimum.w + w};
          blocks[index++] = previous && previous->contains(block)
                                ? previous->blockAt(block)
                                : world.blockAt(block);
        }
      }
    }
  }
  return {minimum, extent, std::move(blocks)};
}

std::vector<FeatureEdge4D> buildFeatureEdges(const BlockWorld &world,
                                             const BlockCoord &center,
                                             int radius) {
  const std::vector<VisibleBoundaryCell4D> visibleCells =
      buildVisibleBoundaryCells(world, center, radius);
  return buildFeatureEdges(visibleCells);
}

std::vector<FeatureEdge4D>
buildFeatureEdges(std::span<const VisibleBoundaryCell4D> cells) {
  std::unordered_map<EdgeKey, EdgeIncidence, EdgeKeyHash> incidences;
  incidences.reserve(cells.size() * 3U);
  for (const VisibleBoundaryCell4D &cell : cells) {
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
        EdgeIncidence &incidence = incidences[{from, edgeAxis}];
        incidence.boundaryNormals.set(normalIndex);
        if (incidence.material == BlockId::air) {
          incidence.material = cell.material;
        }
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
    edges.push_back(
        {toVec4(key.lower), toVec4(upper), key.axis, incidence.material});
  }
  return edges;
}

std::vector<VisibleBoundaryCell4D>
buildVisibleBoundaryCells(const BlockWorld &world, const BlockCoord &center,
                          int radius) {
  return buildVisibleBoundaryCells(buildVisibilityGrid4D(world, center, radius),
                                   center, radius);
}

std::vector<VisibleBoundaryCell4D>
buildVisibleBoundaryCells(const VisibilityGrid4D &grid,
                          const BlockCoord &center, int radius) {
  if (radius < 0) {
    return {};
  }
  std::vector<VisibleBoundaryCell4D> visibleCells;
  for (int w = center.w - radius; w <= center.w + radius; ++w) {
    for (int z = center.z - radius; z <= center.z + radius; ++z) {
      for (int y = center.y - radius; y <= center.y + radius; ++y) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
          const BlockCoord block{x, y, z, w};
          const BlockId material = grid.blockAt(block);
          const bool solid = proj4d::isSolid(material);
          const bool fluid = isFluid(material);
          if (!solid && !fluid) {
            continue;
          }
          for (int axis = 0; axis < 4; ++axis) {
            for (const int side : {-1, 1}) {
              BlockCoord neighbor = block;
              neighbor[static_cast<std::size_t>(axis)] += side;
              const BlockId neighborMaterial = grid.blockAt(neighbor);
              const bool boundaryHidden =
                  solid ? proj4d::isSolid(neighborMaterial)
                        : neighborMaterial == material ||
                              proj4d::isSolid(neighborMaterial);
              if (!boundaryHidden) {
                visibleCells.push_back(
                    {block, axis, side, material,
                     boundaryAmbientOcclusion(grid, block, axis, side)});
              }
            }
          }
        }
      }
    }
  }
  return visibleCells;
}

std::vector<VisibleBoundaryCell4D> buildVisibleBoundaryCellsIncremental(
    const VisibilityGrid4D &grid, const BlockCoord &center, int radius,
    std::span<const VisibleBoundaryCell4D> previousCells,
    const BlockCoord &previousCenter, int previousRadius) {
  if (radius < 0 || previousRadius < 0) {
    return buildVisibleBoundaryCells(grid, center, radius);
  }
  const auto inside = [](const BlockCoord &block, const BlockCoord &origin,
                         int range) {
    return block.x >= origin.x - range && block.x <= origin.x + range &&
           block.y >= origin.y - range && block.y <= origin.y + range &&
           block.z >= origin.z - range && block.z <= origin.z + range &&
           block.w >= origin.w - range && block.w <= origin.w + range;
  };
  std::vector<VisibleBoundaryCell4D> visibleCells;
  visibleCells.reserve(previousCells.size());
  for (const VisibleBoundaryCell4D &cell : previousCells) {
    if (inside(cell.block, center, radius)) {
      visibleCells.push_back(cell);
    }
  }
  for (int w = center.w - radius; w <= center.w + radius; ++w) {
    for (int z = center.z - radius; z <= center.z + radius; ++z) {
      for (int y = center.y - radius; y <= center.y + radius; ++y) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
          const BlockCoord block{x, y, z, w};
          if (inside(block, previousCenter, previousRadius)) {
            continue;
          }
          const BlockId material = grid.blockAt(block);
          const bool solid = proj4d::isSolid(material);
          const bool fluid = isFluid(material);
          if (!solid && !fluid) {
            continue;
          }
          for (int axis = 0; axis < 4; ++axis) {
            for (const int side : {-1, 1}) {
              BlockCoord neighbor = block;
              neighbor[static_cast<std::size_t>(axis)] += side;
              const BlockId neighborMaterial = grid.blockAt(neighbor);
              const bool boundaryHidden =
                  solid ? proj4d::isSolid(neighborMaterial)
                        : neighborMaterial == material ||
                              proj4d::isSolid(neighborMaterial);
              if (!boundaryHidden) {
                visibleCells.push_back(
                    {block, axis, side, material,
                     boundaryAmbientOcclusion(grid, block, axis, side)});
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

std::vector<Line3>
projectVisibleFeatureEdges(const VisibilityGrid4D &grid,
                           std::span<const FeatureEdge4D> edges,
                           const Camera4D &camera) {
  std::vector<Line3> lines;
  lines.reserve(edges.size());
  for (const FeatureEdge4D &edge : edges) {
    if (const auto projected = projectSourceEdge(camera, edge)) {
      appendVisibleFragments(grid, camera, *projected, lines);
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
  return projectFeatureEdges(tesseractEdges(block, BlockId::stone), camera);
}

std::vector<Line3> buildVisibleTesseractWireframe(const BlockWorld &world,
                                                  const BlockCoord &block,
                                                  const Camera4D &camera) {
  return projectVisibleFeatureEdges(
      world, tesseractEdges(block, world.blockAt(block)), camera);
}

std::vector<Line3> buildVisibleTesseractWireframe(const VisibilityGrid4D &grid,
                                                  const BlockCoord &block,
                                                  const Camera4D &camera) {
  return projectVisibleFeatureEdges(
      grid, tesseractEdges(block, grid.blockAt(block)), camera);
}

} // namespace proj4d
