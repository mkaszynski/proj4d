#include "proj4d/volume4d.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

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
  BlockColor color{};
  int firstX{};
  int lastX{};
  int firstY{};
  int lastY{};
};

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

std::vector<ProjectedBoundaryVolume4D>
projectBoundaryVolumes4D(std::span<const VisibleBoundaryCell4D> cells,
                         const Camera4D &camera) {
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
                       int displayHeight, int maximumWidth) {
  displayWidth = std::max(1, displayWidth);
  displayHeight = std::max(1, displayHeight);
  maximumWidth = std::clamp(maximumWidth, 1, maximumVolumeImageWidth);
  if (volumes.size() > 7000U) {
    maximumWidth = std::min(maximumWidth, 104);
  } else if (volumes.size() > 5000U) {
    maximumWidth = std::min(maximumWidth, 120);
  } else if (volumes.size() > 3500U) {
    maximumWidth = std::min(maximumWidth, 136);
  }
  AveragedVolumeImage image;
  image.width = std::min(displayWidth, maximumWidth);
  image.height = std::max(
      1, static_cast<int>(std::lround(static_cast<double>(displayHeight) *
                                      image.width / displayWidth)));
  const std::size_t pixelCount =
      static_cast<std::size_t>(image.width * image.height);
  image.rgba.assign(pixelCount * 4U, 0U);
  std::vector<float> red(pixelCount, 0.0F);
  std::vector<float> green(pixelCount, 0.0F);
  std::vector<float> blue(pixelCount, 0.0F);
  std::vector<float> weight(pixelCount, 0.0F);
  std::vector<PixelRay> rays(pixelCount);

  const Vec3 origin = displayOrigin(camera);
  const double scale =
      static_cast<double>(std::min(image.width, image.height)) * 1.45;
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

  std::vector<PreparedVolume> prepared;
  prepared.reserve(volumes.size());
  for (const ProjectedBoundaryVolume4D &volume : volumes) {
    const auto planes = volumePlanes(volume);
    if (!planes) {
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
    prepared.push_back(
        {*planes, shadedBlockColor(volume.material, volume.brightnessPercent),
         firstX, lastX, firstY, lastY});
  }

  const auto renderRows = [&](int rowBegin, int rowEnd) {
    for (const PreparedVolume &volume : prepared) {
      const int firstY = std::max(rowBegin, volume.firstY);
      const int lastY = std::min(rowEnd - 1, volume.lastY);
      if (firstY > lastY) {
        continue;
      }
      for (int y = firstY; y <= lastY; ++y) {
        for (int x = volume.firstX; x <= volume.lastX; ++x) {
          const std::size_t index =
              static_cast<std::size_t>(y * image.width + x);
          const PixelRay &ray = rays[index];
          if (!ray.intersectsCube) {
            continue;
          }
          double minimum = ray.cubeMinimum;
          double maximum = ray.cubeMaximum;
          if (!intersectPlanes(
                  {volume.planeSet.planes.data(), volume.planeSet.count},
                  origin, ray.direction, minimum, maximum)) {
            continue;
          }
          const float thickness = static_cast<float>(maximum - minimum);
          red[index] += static_cast<float>(volume.color.red) * thickness;
          green[index] += static_cast<float>(volume.color.green) * thickness;
          blue[index] += static_cast<float>(volume.color.blue) * thickness;
          weight[index] += thickness;
        }
      }
    }
  };

  const unsigned hardwareThreads =
      std::max(1U, std::thread::hardware_concurrency());
  const int workerCount =
      std::max(1, std::min({8, static_cast<int>(hardwareThreads),
                            std::max(1, image.height / 16)}));
  std::vector<std::jthread> workers;
  workers.reserve(static_cast<std::size_t>(workerCount));
  for (int worker = 0; worker < workerCount; ++worker) {
    const int begin = image.height * worker / workerCount;
    const int end = image.height * (worker + 1) / workerCount;
    workers.emplace_back(renderRows, begin, end);
  }
  workers.clear();

  for (std::size_t index = 0; index < pixelCount; ++index) {
    if (!rays[index].intersectsCube) {
      image.rgba[index * 4U + 3U] = 0U;
      continue;
    }
    const float cubeThickness =
        static_cast<float>(rays[index].cubeMaximum - rays[index].cubeMinimum);
    const float skyThickness = std::max(0.0F, cubeThickness - weight[index]);
    red[index] += static_cast<float>(skyColor.red) * skyThickness;
    green[index] += static_cast<float>(skyColor.green) * skyThickness;
    blue[index] += static_cast<float>(skyColor.blue) * skyThickness;
    weight[index] += skyThickness;
    if (weight[index] <= 1.0e-6F) {
      image.rgba[index * 4U + 3U] = 255U;
      continue;
    }
    image.rgba[index * 4U] = static_cast<std::uint8_t>(
        std::clamp(std::lround(red[index] / weight[index]), 0L, 255L));
    image.rgba[index * 4U + 1U] = static_cast<std::uint8_t>(
        std::clamp(std::lround(green[index] / weight[index]), 0L, 255L));
    image.rgba[index * 4U + 2U] = static_cast<std::uint8_t>(
        std::clamp(std::lround(blue[index] / weight[index]), 0L, 255L));
    image.rgba[index * 4U + 3U] = 255U;
  }
  return image;
}

} // namespace proj4d
