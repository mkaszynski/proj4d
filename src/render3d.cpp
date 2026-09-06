#include "proj4d/render3d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace proj4d {

int blockBrightnessPercent3D(const BlockCoord3D &block) {
  std::uint32_t hash = 2166136261U;
  hash = (hash ^ static_cast<std::uint32_t>(block.x)) * 16777619U;
  hash = (hash ^ static_cast<std::uint32_t>(block.y)) * 16777619U;
  hash = (hash ^ static_cast<std::uint32_t>(block.z)) * 16777619U;
  hash ^= hash >> 16U;
  constexpr int shadeCount =
      visionMaximumBrightnessPercent3D - visionMinimumBrightnessPercent3D + 1;
  return visionMinimumBrightnessPercent3D +
         static_cast<int>(hash % static_cast<std::uint32_t>(shadeCount));
}

std::array<std::uint8_t, 3> visionBlockColor3D(int worldAxis,
                                               const BlockCoord3D &block) {
  const auto &baseColor =
      visionFaceColors3D.at(static_cast<std::size_t>(worldAxis));
  const int brightness = blockBrightnessPercent3D(block);
  std::array<std::uint8_t, 3> result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    result[channel] = static_cast<std::uint8_t>(std::clamp(
        (static_cast<int>(baseColor[channel]) * brightness + 50) / 100, 0,
        255));
  }
  return result;
}

std::vector<VisionSample3D> buildVisionImage3D(const BlockWorld3D &world,
                                               const Camera3D &camera,
                                               int width, int height,
                                               double maximumDistance) {
  const int radius = std::clamp(static_cast<int>(std::ceil(maximumDistance)), 0,
                                renderBlockRadius3D);
  return rasterizeVisionFaces3D(
      buildVisibleFaces3D(world, containingBlock(camera.position), radius),
      camera, width, height);
}

std::vector<VisibleFace3D> buildVisibleFaces3D(const BlockWorld3D &world,
                                               const BlockCoord3D &center,
                                               int radius) {
  radius = std::max(0, radius);
  std::vector<VisibleFace3D> faces;
  for (int z = center.z - radius; z <= center.z + radius; ++z) {
    for (int y = center.y - radius; y <= center.y + radius; ++y) {
      for (int x = center.x - radius; x <= center.x + radius; ++x) {
        const BlockCoord3D block{x, y, z};
        if (!world.isSolid(block)) {
          continue;
        }
        for (int axis = 0; axis < 3; ++axis) {
          for (const int direction : {-1, 1}) {
            BlockCoord3D neighbor = block;
            neighbor[static_cast<std::size_t>(axis)] += direction;
            if (world.isSolid(neighbor)) {
              continue;
            }
            std::array<int, 2> tangentAxes{};
            int tangentIndex = 0;
            for (int candidate = 0; candidate < 3; ++candidate) {
              if (candidate != axis) {
                tangentAxes[static_cast<std::size_t>(tangentIndex++)] =
                    candidate;
              }
            }
            std::array<Vec3, 4> corners{};
            constexpr std::array<std::array<int, 2>, 4> cornerOffsets{{
                {0, 0},
                {1, 0},
                {1, 1},
                {0, 1},
            }};
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
              Vec3 point{static_cast<double>(x), static_cast<double>(y),
                         static_cast<double>(z)};
              point[static_cast<std::size_t>(axis)] +=
                  direction > 0 ? 1.0 : 0.0;
              point[static_cast<std::size_t>(tangentAxes[0])] +=
                  cornerOffsets[corner][0];
              point[static_cast<std::size_t>(tangentAxes[1])] +=
                  cornerOffsets[corner][1];
              corners[corner] = point;
            }
            faces.push_back({block, axis, direction, corners});
          }
        }
      }
    }
  }
  return faces;
}

namespace {

double cameraDepth(const Camera3D &camera, const Vec3 &point) {
  return dot(point - camera.position, camera.forward());
}

struct ClippedPolygon3D {
  std::array<Vec3, 8> vertices{};
  std::size_t count{};
};

ClippedPolygon3D clipDepthPlane(const ClippedPolygon3D &input,
                                const Camera3D &camera, double plane,
                                bool keepGreater) {
  ClippedPolygon3D output;
  if (input.count == 0U) {
    return output;
  }
  Vec3 previous = input.vertices[input.count - 1U];
  double previousDistance = cameraDepth(camera, previous) - plane;
  bool previousInside =
      keepGreater ? previousDistance >= 0.0 : previousDistance <= 0.0;
  for (std::size_t index = 0; index < input.count; ++index) {
    const Vec3 current = input.vertices[index];
    const double currentDistance = cameraDepth(camera, current) - plane;
    const bool currentInside =
        keepGreater ? currentDistance >= 0.0 : currentDistance <= 0.0;
    if (currentInside != previousInside) {
      const double amount =
          previousDistance / (previousDistance - currentDistance);
      output.vertices[output.count++] =
          previous + (current - previous) * amount;
    }
    if (currentInside) {
      output.vertices[output.count++] = current;
    }
    previous = current;
    previousDistance = currentDistance;
    previousInside = currentInside;
  }
  return output;
}

struct RasterVertex {
  double x{};
  double y{};
  double depth{};
};

double edgeFunction(const RasterVertex &from, const RasterVertex &to, double x,
                    double y) {
  return (x - from.x) * (to.y - from.y) - (y - from.y) * (to.x - from.x);
}

void rasterizeTriangle(const std::array<RasterVertex, 3> &vertices,
                       const VisibleFace3D &face, int width, int height,
                       std::vector<double> &depthBuffer,
                       std::vector<VisionSample3D> &samples) {
  const double area =
      edgeFunction(vertices[0], vertices[1], vertices[2].x, vertices[2].y);
  if (std::abs(area) <= 1.0e-12) {
    return;
  }
  const int minimumX =
      std::clamp(static_cast<int>(std::floor(
                     std::min({vertices[0].x, vertices[1].x, vertices[2].x}))),
                 0, width - 1);
  const int maximumX =
      std::clamp(static_cast<int>(std::ceil(
                     std::max({vertices[0].x, vertices[1].x, vertices[2].x}))),
                 0, width - 1);
  const int minimumY =
      std::clamp(static_cast<int>(std::floor(
                     std::min({vertices[0].y, vertices[1].y, vertices[2].y}))),
                 0, height - 1);
  const int maximumY =
      std::clamp(static_cast<int>(std::ceil(
                     std::max({vertices[0].y, vertices[1].y, vertices[2].y}))),
                 0, height - 1);
  for (int y = minimumY; y <= maximumY; ++y) {
    for (int x = minimumX; x <= maximumX; ++x) {
      const double sampleX = static_cast<double>(x) + 0.5;
      const double sampleY = static_cast<double>(y) + 0.5;
      const double first =
          edgeFunction(vertices[1], vertices[2], sampleX, sampleY);
      const double second =
          edgeFunction(vertices[2], vertices[0], sampleX, sampleY);
      const double third =
          edgeFunction(vertices[0], vertices[1], sampleX, sampleY);
      if ((first < 0.0 || second < 0.0 || third < 0.0) &&
          (first > 0.0 || second > 0.0 || third > 0.0)) {
        continue;
      }
      const double inverseDepth =
          (first / vertices[0].depth + second / vertices[1].depth +
           third / vertices[2].depth) /
          area;
      if (inverseDepth <= 0.0) {
        continue;
      }
      const double depth = 1.0 / inverseDepth;
      const std::size_t index = static_cast<std::size_t>(y * width + x);
      if (depth >= depthBuffer[index]) {
        continue;
      }
      depthBuffer[index] = depth;
      samples[index] = {true, face.worldAxis, depth, face.block};
    }
  }
}

} // namespace

std::vector<ProjectedFace3D>
projectVisibleFaces3D(const std::vector<VisibleFace3D> &faces,
                      const Camera3D &camera, double aspectRatio) {
  std::vector<ProjectedFace3D> projectedFaces;
  projectedFaces.reserve(faces.size());
  for (const VisibleFace3D &face : faces) {
    Vec3 center{};
    for (const Vec3 &corner : face.corners) {
      center += corner * 0.25;
    }
    Vec3 outward{};
    outward[static_cast<std::size_t>(face.worldAxis)] =
        static_cast<double>(face.normalDirection);
    if (dot(camera.position - center, outward) <= 0.0) {
      continue;
    }

    ClippedPolygon3D polygon;
    std::ranges::copy(face.corners, polygon.vertices.begin());
    polygon.count = face.corners.size();
    constexpr double clippingMargin = 1.0e-7;
    polygon = clipDepthPlane(polygon, camera, camera.nearPlane + clippingMargin,
                             true);
    polygon = clipDepthPlane(polygon, camera, camera.farPlane - clippingMargin,
                             false);
    if (polygon.count < 3U) {
      continue;
    }

    ProjectedFace3D projected{face.block, face.worldAxis};
    bool allLeft = true;
    bool allRight = true;
    bool allAbove = true;
    bool allBelow = true;
    for (std::size_t index = 0; index < polygon.count; ++index) {
      const Vec3 &point = polygon.vertices[index];
      const auto vertex = camera.project(point, aspectRatio);
      if (!vertex) {
        projected.vertexCount = 0U;
        break;
      }
      projected.vertices[projected.vertexCount++] = vertex->position;
      projected.depth += vertex->depth;
      allLeft = allLeft && vertex->position.x < -1.0;
      allRight = allRight && vertex->position.x > 1.0;
      allAbove = allAbove && vertex->position.y > 1.0;
      allBelow = allBelow && vertex->position.y < -1.0;
    }
    if (projected.vertexCount < 3U || allLeft || allRight || allAbove ||
        allBelow) {
      continue;
    }
    projected.depth /= static_cast<double>(projected.vertexCount);
    projectedFaces.push_back(std::move(projected));
  }
  return projectedFaces;
}

std::vector<VisionSample3D>
rasterizeVisionFaces3D(const std::vector<VisibleFace3D> &faces,
                       const Camera3D &camera, int width, int height) {
  width = std::max(1, width);
  height = std::max(1, height);
  const double aspectRatio =
      static_cast<double>(width) / static_cast<double>(height);
  const std::size_t pixelCount = static_cast<std::size_t>(width * height);
  std::vector<VisionSample3D> samples(pixelCount);
  std::vector<double> depthBuffer(pixelCount,
                                  std::numeric_limits<double>::infinity());
  for (const VisibleFace3D &face : faces) {
    std::array<RasterVertex, 4> vertices{};
    bool visible = true;
    for (std::size_t corner = 0; corner < face.corners.size(); ++corner) {
      const auto projected = camera.project(face.corners[corner], aspectRatio);
      if (!projected) {
        visible = false;
        break;
      }
      vertices[corner] = {
          (projected->position.x + 1.0) * 0.5 * static_cast<double>(width),
          (1.0 - projected->position.y) * 0.5 * static_cast<double>(height),
          projected->depth,
      };
    }
    if (!visible) {
      continue;
    }
    rasterizeTriangle({vertices[0], vertices[1], vertices[2]}, face, width,
                      height, depthBuffer, samples);
    rasterizeTriangle({vertices[0], vertices[2], vertices[3]}, face, width,
                      height, depthBuffer, samples);
  }
  return samples;
}

std::vector<Line2D> buildCubeSelectionWireframe3D(const BlockCoord3D &block,
                                                  const Camera3D &camera,
                                                  double aspectRatio) {
  std::array<std::optional<ProjectedPoint2D>, 8> projected{};
  for (int corner = 0; corner < 8; ++corner) {
    projected[static_cast<std::size_t>(corner)] =
        camera.project({static_cast<double>(block.x + ((corner & 1) != 0)),
                        static_cast<double>(block.y + ((corner & 2) != 0)),
                        static_cast<double>(block.z + ((corner & 4) != 0))},
                       aspectRatio);
  }

  std::vector<Line2D> lines;
  lines.reserve(12);
  for (int corner = 0; corner < 8; ++corner) {
    for (int axis = 0; axis < 3; ++axis) {
      const int mask = 1 << axis;
      if ((corner & mask) != 0) {
        continue;
      }
      const int other = corner | mask;
      const auto &from = projected[static_cast<std::size_t>(corner)];
      const auto &to = projected[static_cast<std::size_t>(other)];
      if (from && to) {
        lines.push_back({from->position, to->position});
      }
    }
  }
  return lines;
}

} // namespace proj4d
