#include "proj4d/volume4d.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>
#include <utility>

#include "proj4d/texture_atlas.hpp"

namespace proj4d {

namespace {

Vec3 cross(const Vec3 &left, const Vec3 &right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

std::array<int, 3> tangentAxes(int boundaryAxis) {
  std::array<int, 3> axes{};
  int output = 0;
  for (int axis = 0; axis < 4; ++axis) {
    if (axis != boundaryAxis) {
      axes[static_cast<std::size_t>(output++)] = axis;
    }
  }
  return axes;
}

Vec4 boundaryVertex(const VisibleBoundaryCell4D &cell, int mask) {
  Vec4 point{
      static_cast<double>(cell.block.x), static_cast<double>(cell.block.y),
      static_cast<double>(cell.block.z), static_cast<double>(cell.block.w)};
  point[static_cast<std::size_t>(cell.axis)] += cell.side > 0 ? 1.0 : 0.0;
  const auto axes = tangentAxes(cell.axis);
  for (int bit = 0; bit < 3; ++bit) {
    if ((mask & (1 << bit)) != 0) {
      point[static_cast<std::size_t>(axes[static_cast<std::size_t>(bit)])] +=
          1.0;
    }
  }
  return point;
}

Vec3 rotateForDisplay(const Vec3 &point, const VolumeDisplayCamera &camera) {
  const double yawCosine = std::cos(camera.yaw);
  const double yawSine = std::sin(camera.yaw);
  const Vec3 yawed{point.x * yawCosine + point.z * yawSine, point.y,
                   -point.x * yawSine + point.z * yawCosine};
  const double pitchCosine = std::cos(camera.pitch);
  const double pitchSine = std::sin(camera.pitch);
  return {yawed.x, yawed.y * pitchCosine - yawed.z * pitchSine,
          yawed.y * pitchSine + yawed.z * pitchCosine};
}

Vec3 inverseRotateDirection(const Vec3 &direction,
                            const VolumeDisplayCamera &camera) {
  const double pitchCosine = std::cos(camera.pitch);
  const double pitchSine = std::sin(camera.pitch);
  const Vec3 unpitched{direction.x,
                       direction.y * pitchCosine + direction.z * pitchSine,
                       -direction.y * pitchSine + direction.z * pitchCosine};
  const double yawCosine = std::cos(camera.yaw);
  const double yawSine = std::sin(camera.yaw);
  return {unpitched.x * yawCosine - unpitched.z * yawSine, unpitched.y,
          unpitched.x * yawSine + unpitched.z * yawCosine};
}

Vec3 displayOrigin(const VolumeDisplayCamera &camera) {
  return inverseRotateDirection({0.0, 0.0, camera.distance}, camera);
}

struct Plane {
  Vec3 normal{};
  double offset{};
};

struct VolumePlaneSet {
  std::array<Plane, 32> planes{};
  std::size_t count{};
};

std::optional<VolumePlaneSet>
volumePlanes(const ProjectedBoundaryVolume4D &volume) {
  if (volume.vertexCount < 4U) {
    return std::nullopt;
  }
  VolumePlaneSet result;
  Vec3 center{};
  for (std::size_t index = 0; index < volume.vertexCount; ++index) {
    center += volume.vertices[index] *
              (1.0 / static_cast<double>(volume.vertexCount));
  }
  if (!volume.canonicalCube) {
    constexpr double epsilon = 1.0e-8;
    for (std::size_t first = 0; first < volume.vertexCount; ++first) {
      for (std::size_t second = first + 1U; second < volume.vertexCount;
           ++second) {
        for (std::size_t third = second + 1U; third < volume.vertexCount;
             ++third) {
          const Vec3 origin = volume.vertices[first];
          Vec3 normal = cross(volume.vertices[second] - origin,
                              volume.vertices[third] - origin);
          const double magnitude = length(normal);
          if (magnitude <= epsilon) {
            continue;
          }
          normal = normal * (1.0 / magnitude);
          double offset = -dot(normal, origin);
          bool positive = false;
          bool negative = false;
          for (std::size_t index = 0; index < volume.vertexCount; ++index) {
            const double distance =
                dot(normal, volume.vertices[index]) + offset;
            positive = positive || distance > epsilon;
            negative = negative || distance < -epsilon;
          }
          if (positive && negative) {
            continue;
          }
          if (dot(normal, center) + offset > 0.0) {
            normal = normal * -1.0;
            offset = -offset;
          }
          bool duplicate = false;
          for (std::size_t index = 0; index < result.count; ++index) {
            const Plane &plane = result.planes[index];
            if (length(plane.normal - normal) <= 1.0e-6 &&
                std::abs(plane.offset - offset) <= 1.0e-6) {
              duplicate = true;
              break;
            }
          }
          if (!duplicate && result.count < result.planes.size()) {
            result.planes[result.count++] = {normal, offset};
          }
        }
      }
    }
    return result.count >= 4U ? std::optional(result) : std::nullopt;
  }
  for (int axis = 0; axis < 3; ++axis) {
    const int otherFirst = (axis + 1) % 3;
    const int otherSecond = (axis + 2) % 3;
    for (int side = 0; side < 2; ++side) {
      const int base = side << axis;
      const Vec3 first = volume.vertices[static_cast<std::size_t>(base)];
      const Vec3 second =
          volume.vertices[static_cast<std::size_t>(base | (1 << otherFirst))];
      const Vec3 third =
          volume.vertices[static_cast<std::size_t>(base | (1 << otherSecond))];
      Vec3 normal = cross(second - first, third - first);
      if (dot(normal, normal) <= 1.0e-16) {
        return std::nullopt;
      }
      if (dot(normal, center - first) > 0.0) {
        normal = normal * -1.0;
      }
      result.planes[result.count++] = {normal, -dot(normal, first)};
    }
  }
  return result;
}

bool intersectPlanes(std::span<const Plane> planes, const Vec3 &origin,
                     const Vec3 &direction, double &minimum, double &maximum) {
  constexpr double epsilon = 1.0e-10;
  for (const Plane &plane : planes) {
    const double originDistance = dot(plane.normal, origin) + plane.offset;
    const double slope = dot(plane.normal, direction);
    if (std::abs(slope) <= epsilon) {
      if (originDistance > 0.0) {
        return false;
      }
      continue;
    }
    const double crossing = -originDistance / slope;
    if (slope < 0.0) {
      minimum = std::max(minimum, crossing);
    } else {
      maximum = std::min(maximum, crossing);
    }
    if (minimum >= maximum) {
      return false;
    }
  }
  return maximum > std::max(minimum, 0.0);
}

bool pointInsidePlanes(std::span<const Plane> planes, const Vec3 &point) {
  return std::ranges::all_of(planes, [&](const Plane &plane) {
    return dot(plane.normal, point) + plane.offset <= 1.0e-9;
  });
}

std::vector<Vec3> centerSlicePolygon(std::span<const Plane> planes) {
  std::vector<Vec3> polygon{
      {-1.0, 1.0, 0.0},
      {1.0, 1.0, 0.0},
      {1.0, -1.0, 0.0},
      {-1.0, -1.0, 0.0},
  };
  std::vector<Vec3> clipped;
  for (const Plane &plane : planes) {
    if (polygon.empty()) {
      break;
    }
    clipped.clear();
    clipped.reserve(polygon.size() + 1U);
    Vec3 previous = polygon.back();
    double previousDistance = dot(plane.normal, previous) + plane.offset;
    bool previousInside = previousDistance <= 1.0e-9;
    for (const Vec3 &current : polygon) {
      const double currentDistance = dot(plane.normal, current) + plane.offset;
      const bool currentInside = currentDistance <= 1.0e-9;
      if (currentInside != previousInside) {
        const double denominator = previousDistance - currentDistance;
        if (std::abs(denominator) > 1.0e-12) {
          clipped.push_back(previous + (current - previous) *
                                           (previousDistance / denominator));
        }
      }
      if (currentInside) {
        clipped.push_back(current);
      }
      previous = current;
      previousDistance = currentDistance;
      previousInside = currentInside;
    }
    polygon.swap(clipped);
  }
  return polygon;
}

std::array<Plane, 6> visionCubePlanes() {
  return {{{{1.0, 0.0, 0.0}, -1.0},
           {{-1.0, 0.0, 0.0}, -1.0},
           {{0.0, 1.0, 0.0}, -1.0},
           {{0.0, -1.0, 0.0}, -1.0},
           {{0.0, 0.0, 1.0}, -1.0},
           {{0.0, 0.0, -1.0}, -1.0}}};
}

struct PixelRay {
  Vec3 direction{};
  double cubeMinimum{};
  double cubeMaximum{};
  bool intersectsCube{};
};

struct PreparedVolume {
  VolumePlaneSet planeSet{};
  BlockId material{BlockId::air};
  BlockColor color{};
  int brightnessPercent{100};
  double opacity{1.0};
  double sourceDepthNumerator{};
  double sourceDepthDenominatorConstant{};
  Vec3 sourceDepthDenominatorGradient{};
  bool hasSourceDepth{};
  std::uint32_t ambientOcclusion{};
  Vec3 sourceTextureOffset{};
  Vec3 sourceTextureForward{};
  Vec3 sourceTextureImageX{};
  Vec3 sourceTextureImageY{};
  Vec3 sourceTextureImageZ{};
  bool hasSourceTexture{};
  bool fluid{};
  int firstX{};
  int lastX{};
  int firstY{};
  int lastY{};
};

struct VolumeEvent {
  std::size_t volume{};
  double position{};
  bool entering{};
};

double sourceDepthAt(const PreparedVolume &volume, const Vec3 &point) {
  const double denominator = volume.sourceDepthDenominatorConstant +
                             dot(volume.sourceDepthDenominatorGradient, point);
  if (std::abs(denominator) <= 1.0e-12) {
    return std::numeric_limits<double>::infinity();
  }
  const double depth = volume.sourceDepthNumerator / denominator;
  return depth > 0.0 && std::isfinite(depth)
             ? depth
             : std::numeric_limits<double>::infinity();
}

Vec3 sourceTextureCoordinateAt(const PreparedVolume &volume,
                               const Vec3 &point) {
  const double depth = sourceDepthAt(volume, point);
  if (!std::isfinite(depth)) {
    return {};
  }
  const Vec3 direction = volume.sourceTextureForward +
                         volume.sourceTextureImageX * point.x +
                         volume.sourceTextureImageY * point.y +
                         volume.sourceTextureImageZ * point.z;
  const Vec3 coordinate = volume.sourceTextureOffset + direction * depth;
  return {std::clamp(coordinate.x, 0.0, 1.0),
          std::clamp(coordinate.y, 0.0, 1.0),
          std::clamp(coordinate.z, 0.0, 1.0)};
}

double ambientOcclusionBrightness(const PreparedVolume &volume,
                                  const Vec3 &coordinate) {
  double occlusion = 0.0;
  for (int corner = 0; corner < 8; ++corner) {
    double weight = 1.0;
    for (int bit = 0; bit < 3; ++bit) {
      const double amount = coordinate[static_cast<std::size_t>(bit)];
      weight *= (corner & (1 << bit)) != 0 ? amount : 1.0 - amount;
    }
    occlusion += weight * ambientOcclusionAt4D(volume.ambientOcclusion, corner);
  }
  return std::clamp(1.0 - occlusion * (0.45 / 7.0), 0.55, 1.0);
}

} // namespace

int boundaryBrightnessPercent4D(int axis, int side) {
  constexpr std::array<std::array<int, 2>, 4> brightness{{
      {{88, 102}},
      {{72, 124}},
      {{84, 108}},
      {{92, 114}},
  }};
  axis = std::clamp(axis, 0, 3);
  return brightness[static_cast<std::size_t>(axis)]
                   [static_cast<std::size_t>(side > 0 ? 1 : 0)];
}

void setProjectedSourcePlane4D(ProjectedBoundaryVolume4D &volume,
                               const Camera4D &camera, int axis,
                               double planeCoordinate) {
  axis = std::clamp(axis, 0, 3);
  const std::size_t sourceAxis = static_cast<std::size_t>(axis);
  volume.sourceDepthNumerator = planeCoordinate - camera.position[sourceAxis];
  volume.sourceDepthDenominatorConstant = camera.forward[sourceAxis];
  volume.sourceDepthDenominatorGradient = {
      camera.imageX[sourceAxis] / camera.focalLength,
      camera.imageY[sourceAxis] / camera.focalLength,
      camera.imageZ[sourceAxis] / camera.focalLength,
  };
  volume.hasSourceDepth = true;
}

std::vector<ProjectedBoundaryVolume4D>
projectBoundaryVolumes4D(std::span<const VisibleBoundaryCell4D> cells,
                         const Camera4D &camera, bool includeSourceTexture) {
  std::vector<ProjectedBoundaryVolume4D> volumes;
  volumes.reserve(
      std::min<std::size_t>(cells.size(), maximumProjectedBoundaryVolumes));
  for (const VisibleBoundaryCell4D &cell : cells) {
    if (volumes.size() >= maximumProjectedBoundaryVolumes) {
      break;
    }
    ProjectedBoundaryVolume4D volume;
    volume.material = cell.material;
    volume.brightnessPercent =
        boundaryBrightnessPercent4D(cell.axis, cell.side);
    volume.ambientOcclusion = cell.ambientOcclusion;
    setProjectedSourcePlane4D(
        volume, camera, cell.axis,
        static_cast<double>(cell.block[static_cast<std::size_t>(cell.axis)] +
                            (cell.side > 0 ? 1 : 0)));
    if (includeSourceTexture) {
      const std::array<int, 3> axes = tangentAxes(cell.axis);
      for (int index = 0; index < 3; ++index) {
        const std::size_t output = static_cast<std::size_t>(index);
        const std::size_t source =
            static_cast<std::size_t>(axes[static_cast<std::size_t>(index)]);
        volume.sourceTextureOffset[output] =
            camera.position[source] - cell.block[source];
        volume.sourceTextureForward[output] = camera.forward[source];
        volume.sourceTextureImageX[output] =
            camera.imageX[source] / camera.focalLength;
        volume.sourceTextureImageY[output] =
            camera.imageY[source] / camera.focalLength;
        volume.sourceTextureImageZ[output] =
            camera.imageZ[source] / camera.focalLength;
      }
      volume.hasSourceTexture = true;
    }
    std::array<Vec4, 8> sourceVertices{};
    std::array<double, 8> depths{};
    bool entirelyInside = true;
    for (int mask = 0; mask < 8; ++mask) {
      sourceVertices[static_cast<std::size_t>(mask)] =
          boundaryVertex(cell, mask);
      depths[static_cast<std::size_t>(mask)] =
          dot(sourceVertices[static_cast<std::size_t>(mask)] - camera.position,
              camera.forward);
      entirelyInside =
          entirelyInside &&
          depths[static_cast<std::size_t>(mask)] >= camera.nearPlane &&
          depths[static_cast<std::size_t>(mask)] <= camera.farPlane;
    }
    if (entirelyInside) {
      for (int mask = 0; mask < 8; ++mask) {
        const auto projected =
            camera.project(sourceVertices[static_cast<std::size_t>(mask)]);
        if (!projected) {
          entirelyInside = false;
          break;
        }
        volume.vertices[static_cast<std::size_t>(mask)] = projected->position;
      }
    }
    if (entirelyInside) {
      volumes.push_back(volume);
      continue;
    }

    volume.vertexCount = 0U;
    volume.canonicalCube = false;
    constexpr double clippingMargin = 1.0e-6;
    const double nearDepth = camera.nearPlane + clippingMargin;
    const double farDepth = camera.farPlane - clippingMargin;
    const auto appendProjected = [&](const Vec4 &point) {
      const auto projected = camera.project(point);
      if (!projected || volume.vertexCount >= volume.vertices.size()) {
        return;
      }
      for (std::size_t index = 0; index < volume.vertexCount; ++index) {
        if (length(volume.vertices[index] - projected->position) <= 1.0e-9) {
          return;
        }
      }
      volume.vertices[volume.vertexCount++] = projected->position;
    };
    for (std::size_t index = 0; index < sourceVertices.size(); ++index) {
      if (depths[index] >= nearDepth && depths[index] <= farDepth) {
        appendProjected(sourceVertices[index]);
      }
    }
    for (int mask = 0; mask < 8; ++mask) {
      for (int bit = 0; bit < 3; ++bit) {
        if ((mask & (1 << bit)) != 0) {
          continue;
        }
        const int other = mask | (1 << bit);
        const double firstDepth = depths[static_cast<std::size_t>(mask)];
        const double secondDepth = depths[static_cast<std::size_t>(other)];
        const double change = secondDepth - firstDepth;
        if (std::abs(change) <= 1.0e-12) {
          continue;
        }
        for (const double plane : {nearDepth, farDepth}) {
          const double amount = (plane - firstDepth) / change;
          if (amount > 0.0 && amount < 1.0) {
            appendProjected(
                lerp(sourceVertices[static_cast<std::size_t>(mask)],
                     sourceVertices[static_cast<std::size_t>(other)], amount));
          }
        }
      }
    }
    if (volume.vertexCount >= 4U) {
      volumes.push_back(volume);
    }
  }
  return volumes;
}

AveragedVolumeImage
renderAveragedVolume4D(std::span<const ProjectedBoundaryVolume4D> volumes,
                       const VolumeDisplayCamera &camera, int displayWidth,
                       int displayHeight, int maximumWidth,
                       BlockId immersionMaterial, VolumeViewMode viewMode) {
  displayWidth = std::max(1, displayWidth);
  displayHeight = std::max(1, displayHeight);
  maximumWidth = std::clamp(maximumWidth, 1,
                            viewMode == VolumeViewMode::CenterSlice
                                ? maximumSliceImageWidth
                                : maximumVolumeImageWidth);
  if (viewMode == VolumeViewMode::Solid) {
    if (volumes.size() > 7000U) {
      maximumWidth = std::min(maximumWidth, 104);
    } else if (volumes.size() > 5000U) {
      maximumWidth = std::min(maximumWidth, 120);
    } else if (volumes.size() > 3500U) {
      maximumWidth = std::min(maximumWidth, 136);
    }
  }
  AveragedVolumeImage image;
  if (viewMode == VolumeViewMode::CenterSlice) {
    image.width = std::min({displayWidth, displayHeight, maximumWidth});
    image.height = image.width;
  } else {
    image.width = std::min(displayWidth, maximumWidth);
    image.height = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(displayHeight) *
                                        image.width / displayWidth)));
  }
  const std::size_t pixelCount =
      static_cast<std::size_t>(image.width * image.height);
  image.rgba.assign(pixelCount * 4U, 0U);
  std::vector<double> red(pixelCount, 0.0);
  std::vector<double> green(pixelCount, 0.0);
  std::vector<double> blue(pixelCount, 0.0);
  std::vector<double> weight(pixelCount, 0.0);
  std::vector<PixelRay> rays;

  const Vec3 origin = displayOrigin(camera);
  const double scale =
      static_cast<double>(std::min(image.width, image.height)) * 1.45;
  if (viewMode == VolumeViewMode::Solid) {
    rays.resize(pixelCount);
    const auto cubePlanes = visionCubePlanes();
    for (int y = 0; y < image.height; ++y) {
      for (int x = 0; x < image.width; ++x) {
        const Vec3 displayDirection{
            (static_cast<double>(x) + 0.5 - image.width * 0.5) / scale,
            -(static_cast<double>(y) + 0.5 - image.height * 0.5) / scale, -1.0};
        PixelRay &ray = rays[static_cast<std::size_t>(y * image.width + x)];
        ray.direction =
            normalized(inverseRotateDirection(displayDirection, camera));
        ray.cubeMinimum = 0.0;
        ray.cubeMaximum = std::numeric_limits<double>::infinity();
        ray.intersectsCube = intersectPlanes(cubePlanes, origin, ray.direction,
                                             ray.cubeMinimum, ray.cubeMaximum);
      }
    }
  }

  std::vector<PreparedVolume> prepared;
  prepared.reserve(volumes.size());
  for (const ProjectedBoundaryVolume4D &volume : volumes) {
    const auto planes = volumePlanes(volume);
    if (!planes) {
      continue;
    }
    const int ambientBrightness =
        averageAmbientOcclusionPercent4D(volume.ambientOcclusion);
    PreparedVolume preparedVolume{
        *planes,
        volume.material,
        shadedBlockColor(volume.material,
                         volume.brightnessPercent * ambientBrightness / 100),
        volume.brightnessPercent,
        static_cast<double>(blockOpacity(volume.material)) / 255.0,
        volume.sourceDepthNumerator,
        volume.sourceDepthDenominatorConstant,
        volume.sourceDepthDenominatorGradient,
        volume.hasSourceDepth,
        volume.ambientOcclusion,
        volume.sourceTextureOffset,
        volume.sourceTextureForward,
        volume.sourceTextureImageX,
        volume.sourceTextureImageY,
        volume.sourceTextureImageZ,
        volume.hasSourceTexture,
        isFluid(volume.material),
    };
    if (viewMode == VolumeViewMode::CenterSlice) {
      prepared.push_back(std::move(preparedVolume));
      continue;
    }
    double minimumX = static_cast<double>(image.width);
    double maximumX = 0.0;
    double minimumY = static_cast<double>(image.height);
    double maximumY = 0.0;
    bool visible = false;
    for (std::size_t vertexIndex = 0; vertexIndex < volume.vertexCount;
         ++vertexIndex) {
      const Vec3 &vertex = volume.vertices[vertexIndex];
      const Vec3 rotated = rotateForDisplay(vertex, camera);
      const double depth = camera.distance - rotated.z;
      if (depth <= 0.05) {
        continue;
      }
      const double x = image.width * 0.5 + rotated.x * scale / depth;
      const double y = image.height * 0.5 - rotated.y * scale / depth;
      minimumX = std::min(minimumX, x);
      maximumX = std::max(maximumX, x);
      minimumY = std::min(minimumY, y);
      maximumY = std::max(maximumY, y);
      visible = true;
    }
    if (!visible) {
      continue;
    }
    const int firstX = std::clamp(static_cast<int>(std::floor(minimumX)) - 1, 0,
                                  image.width - 1);
    const int lastX = std::clamp(static_cast<int>(std::ceil(maximumX)) + 1, 0,
                                 image.width - 1);
    const int firstY = std::clamp(static_cast<int>(std::floor(minimumY)) - 1, 0,
                                  image.height - 1);
    const int lastY = std::clamp(static_cast<int>(std::ceil(maximumY)) + 1, 0,
                                 image.height - 1);
    preparedVolume.firstX = firstX;
    preparedVolume.lastX = lastX;
    preparedVolume.firstY = firstY;
    preparedVolume.lastY = lastY;
    prepared.push_back(std::move(preparedVolume));
  }

  const auto compositeAtPoint = [&](std::span<const std::size_t> activeVolumes,
                                    const Vec3 &point, double measure,
                                    double &pixelRed, double &pixelGreen,
                                    double &pixelBlue, double &pixelWeight) {
    std::size_t nearestOpaque = prepared.size();
    double nearestOpaqueDepth = std::numeric_limits<double>::infinity();
    for (const std::size_t volumeIndex : activeVolumes) {
      const PreparedVolume &volume = prepared[volumeIndex];
      if (!volume.hasSourceDepth || volume.fluid) {
        continue;
      }
      const double depth = sourceDepthAt(volume, point);
      if (depth < nearestOpaqueDepth) {
        nearestOpaqueDepth = depth;
        nearestOpaque = volumeIndex;
      }
    }

    double coveredOpacity = 0.0;
    const auto addVolume = [&](const PreparedVolume &volume) {
      BlockColor sourceColor = volume.color;
      double opacity = volume.opacity;
      if (viewMode == VolumeViewMode::CenterSlice && volume.hasSourceTexture) {
        const Vec3 coordinate = sourceTextureCoordinateAt(volume, point);
        const TexturePixel texture = sampleMaterialTexture3D(
            volume.material, coordinate.x, coordinate.y, coordinate.z);
        const int oriented =
            std::clamp(static_cast<int>(std::lround(
                           ambientOcclusionBrightness(volume, coordinate) *
                           volume.brightnessPercent)),
                       0, 140);
        sourceColor = {
            static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(texture.red) * oriented / 100, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(texture.green) * oriented / 100, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(texture.blue) * oriented / 100, 0, 255)),
        };
        opacity *= static_cast<double>(texture.alpha) / 255.0;
      }
      const double contribution = measure * opacity;
      pixelRed += static_cast<double>(sourceColor.red) * contribution;
      pixelGreen += static_cast<double>(sourceColor.green) * contribution;
      pixelBlue += static_cast<double>(sourceColor.blue) * contribution;
      pixelWeight += contribution;
      coveredOpacity += opacity;
    };
    for (const std::size_t volumeIndex : activeVolumes) {
      const PreparedVolume &volume = prepared[volumeIndex];
      if (!volume.hasSourceDepth) {
        addVolume(volume);
        continue;
      }
      if (!volume.fluid) {
        if (volumeIndex == nearestOpaque) {
          addVolume(volume);
        }
        continue;
      }
      if (sourceDepthAt(volume, point) <= nearestOpaqueDepth + 1.0e-8) {
        addVolume(volume);
      }
    }
    const double skyContribution =
        measure * std::max(0.0, 1.0 - coveredOpacity);
    pixelRed += static_cast<double>(skyColor.red) * skyContribution;
    pixelGreen += static_cast<double>(skyColor.green) * skyContribution;
    pixelBlue += static_cast<double>(skyColor.blue) * skyContribution;
    pixelWeight += skyContribution;
  };

  const unsigned hardwareThreads =
      std::max(1U, std::thread::hardware_concurrency());
  const int workerCount =
      std::max(1, std::min({8, static_cast<int>(hardwareThreads),
                            std::max(1, image.height / 16)}));
  const auto runWorkers = [&](const auto &renderRows) {
    std::vector<std::jthread> workers;
    workers.reserve(static_cast<std::size_t>(workerCount));
    for (int worker = 0; worker < workerCount; ++worker) {
      const int begin = image.height * worker / workerCount;
      const int end = image.height * (worker + 1) / workerCount;
      workers.emplace_back(renderRows, begin, end);
    }
  };

  std::vector<std::vector<std::size_t>> rowCandidates(
      static_cast<std::size_t>(image.height));
  if (viewMode == VolumeViewMode::CenterSlice) {
    for (std::size_t volumeIndex = 0; volumeIndex < prepared.size();
         ++volumeIndex) {
      PreparedVolume &volume = prepared[volumeIndex];
      const std::vector<Vec3> polygon = centerSlicePolygon(
          {volume.planeSet.planes.data(), volume.planeSet.count});
      if (polygon.empty()) {
        volume.lastY = -1;
        continue;
      }
      double minimumX = 1.0;
      double maximumX = -1.0;
      double minimumY = 1.0;
      double maximumY = -1.0;
      for (const Vec3 &point : polygon) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
      }
      volume.firstX = std::clamp(
          static_cast<int>(std::floor((minimumX + 1.0) * image.width * 0.5)) -
              1,
          0, image.width - 1);
      volume.lastX = std::clamp(
          static_cast<int>(std::ceil((maximumX + 1.0) * image.width * 0.5)) + 1,
          0, image.width - 1);
      volume.firstY = std::clamp(
          static_cast<int>(std::floor((1.0 - maximumY) * image.height * 0.5)) -
              1,
          0, image.height - 1);
      volume.lastY = std::clamp(
          static_cast<int>(std::ceil((1.0 - minimumY) * image.height * 0.5)) +
              1,
          0, image.height - 1);
      for (int y = volume.firstY; y <= volume.lastY; ++y) {
        rowCandidates[static_cast<std::size_t>(y)].push_back(volumeIndex);
      }
    }
    const auto renderSliceRows = [&](int rowBegin, int rowEnd) {
      std::vector<std::size_t> active;
      for (int y = rowBegin; y < rowEnd; ++y) {
        const auto &candidates = rowCandidates[static_cast<std::size_t>(y)];
        const double pointY =
            1.0 - (static_cast<double>(y) + 0.5) * 2.0 / image.height;
        for (int x = 0; x < image.width; ++x) {
          const std::size_t index =
              static_cast<std::size_t>(y * image.width + x);
          const Vec3 point{-1.0 + (static_cast<double>(x) + 0.5) * 2.0 /
                                      image.width,
                           pointY, 0.0};
          active.clear();
          for (const std::size_t volumeIndex : candidates) {
            const PreparedVolume &volume = prepared[volumeIndex];
            if (x < volume.firstX || x > volume.lastX ||
                !pointInsidePlanes(
                    {volume.planeSet.planes.data(), volume.planeSet.count},
                    point)) {
              continue;
            }
            active.push_back(volumeIndex);
          }
          compositeAtPoint(active, point, 1.0, red[index], green[index],
                           blue[index], weight[index]);
        }
      }
    };
    runWorkers(renderSliceRows);
  } else {
    for (std::size_t volume = 0; volume < prepared.size(); ++volume) {
      for (int y = prepared[volume].firstY; y <= prepared[volume].lastY; ++y) {
        rowCandidates[static_cast<std::size_t>(y)].push_back(volume);
      }
    }
    const auto renderSolidRows = [&](int rowBegin, int rowEnd) {
      std::vector<VolumeEvent> events;
      std::vector<std::size_t> active;
      for (int y = rowBegin; y < rowEnd; ++y) {
        const auto &candidates = rowCandidates[static_cast<std::size_t>(y)];
        for (int x = 0; x < image.width; ++x) {
          const std::size_t index =
              static_cast<std::size_t>(y * image.width + x);
          const PixelRay &ray = rays[index];
          if (!ray.intersectsCube) {
            continue;
          }
          events.clear();
          active.clear();
          for (const std::size_t volumeIndex : candidates) {
            const PreparedVolume &volume = prepared[volumeIndex];
            if (x < volume.firstX || x > volume.lastX) {
              continue;
            }
            double minimum = ray.cubeMinimum;
            double maximum = ray.cubeMaximum;
            if (!intersectPlanes(
                    {volume.planeSet.planes.data(), volume.planeSet.count},
                    origin, ray.direction, minimum, maximum) ||
                maximum - minimum <= 1.0e-9) {
              continue;
            }
            events.push_back({volumeIndex, minimum, true});
            events.push_back({volumeIndex, maximum, false});
          }
          if (events.empty()) {
            const double thickness = ray.cubeMaximum - ray.cubeMinimum;
            red[index] = static_cast<double>(skyColor.red) * thickness;
            green[index] = static_cast<double>(skyColor.green) * thickness;
            blue[index] = static_cast<double>(skyColor.blue) * thickness;
            weight[index] = thickness;
            continue;
          }
          std::ranges::sort(events, std::less{}, &VolumeEvent::position);

          double pixelRed = 0.0;
          double pixelGreen = 0.0;
          double pixelBlue = 0.0;
          double pixelWeight = 0.0;
          const auto compositeSegment = [&](double minimum, double maximum) {
            const double thickness = maximum - minimum;
            if (thickness <= 1.0e-9) {
              return;
            }
            const double midpoint = (minimum + maximum) * 0.5;
            const Vec3 point = origin + ray.direction * midpoint;
            compositeAtPoint(active, point, thickness, pixelRed, pixelGreen,
                             pixelBlue, pixelWeight);
          };

          double position = ray.cubeMinimum;
          std::size_t eventIndex = 0U;
          while (eventIndex < events.size() &&
                 events[eventIndex].position <= position + 1.0e-9) {
            if (events[eventIndex].entering) {
              active.push_back(events[eventIndex].volume);
            }
            ++eventIndex;
          }
          while (position < ray.cubeMaximum - 1.0e-9) {
            const double next =
                eventIndex < events.size()
                    ? std::min(events[eventIndex].position, ray.cubeMaximum)
                    : ray.cubeMaximum;
            compositeSegment(position, next);
            position = next;
            const std::size_t groupBegin = eventIndex;
            while (eventIndex < events.size() &&
                   std::abs(events[eventIndex].position - position) <= 1.0e-9) {
              ++eventIndex;
            }
            for (std::size_t event = groupBegin; event < eventIndex; ++event) {
              if (events[event].entering) {
                active.push_back(events[event].volume);
                continue;
              }
              const auto found =
                  std::ranges::find(active, events[event].volume);
              if (found != active.end()) {
                active.erase(found);
              }
            }
            if (next >= ray.cubeMaximum) {
              break;
            }
          }
          red[index] = pixelRed;
          green[index] = pixelGreen;
          blue[index] = pixelBlue;
          weight[index] = pixelWeight;
        }
      }
    };
    runWorkers(renderSolidRows);
  }

  for (std::size_t index = 0; index < pixelCount; ++index) {
    if ((viewMode == VolumeViewMode::Solid && !rays[index].intersectsCube) ||
        weight[index] <= 1.0e-6) {
      image.rgba[index * 4U + 3U] = 0U;
      continue;
    }
    image.rgba[index * 4U] = static_cast<std::uint8_t>(
        std::clamp(std::lround(red[index] / weight[index]), 0L, 255L));
    image.rgba[index * 4U + 1U] = static_cast<std::uint8_t>(
        std::clamp(std::lround(green[index] / weight[index]), 0L, 255L));
    image.rgba[index * 4U + 2U] = static_cast<std::uint8_t>(
        std::clamp(std::lround(blue[index] / weight[index]), 0L, 255L));
    if (isFluid(immersionMaterial)) {
      const BlockColor immersionColor = blockColor(immersionMaterial);
      const int alpha = blockOpacity(immersionMaterial) / 3;
      image.rgba[index * 4U] = static_cast<std::uint8_t>(
          (static_cast<int>(immersionColor.red) * alpha +
           static_cast<int>(image.rgba[index * 4U]) * (255 - alpha) + 127) /
          255);
      image.rgba[index * 4U + 1U] = static_cast<std::uint8_t>(
          (static_cast<int>(immersionColor.green) * alpha +
           static_cast<int>(image.rgba[index * 4U + 1U]) * (255 - alpha) +
           127) /
          255);
      image.rgba[index * 4U + 2U] = static_cast<std::uint8_t>(
          (static_cast<int>(immersionColor.blue) * alpha +
           static_cast<int>(image.rgba[index * 4U + 2U]) * (255 - alpha) +
           127) /
          255);
    }
    image.rgba[index * 4U + 3U] = 255U;
  }
  return image;
}

} // namespace proj4d
