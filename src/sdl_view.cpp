#include "proj4d/sdl_view.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/camera2d.hpp"
#include "proj4d/camera3d.hpp"
#include "proj4d/clouds.hpp"
#include "proj4d/game_session.hpp"
#include "proj4d/mouse_input.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/player_motion2d.hpp"
#include "proj4d/player_motion3d.hpp"
#include "proj4d/render2d.hpp"
#include "proj4d/render3d.hpp"
#include "proj4d/render_geometry.hpp"
#include "proj4d/view_status.hpp"
#include "proj4d/volume4d.hpp"
#include "proj4d/world.hpp"
#include "proj4d/world2d.hpp"
#include "proj4d/world3d.hpp"
#include "proj4d/world_save.hpp"

namespace proj4d {

namespace {

constexpr double pi = 3.14159265358979323846;

struct DisplayCamera {
  double yaw{-0.58};
  double pitch{0.42};
  double distance{3.8};
};

struct ScreenPoint {
  int x{};
  int y{};
};

struct FeatureGeometryCache {
  std::uint64_t startupCounter{SDL_GetPerformanceCounter()};
  bool initialStartupReported{};
  bool fullStartupReported{};
  BlockCoord center{};
  int radius{-1};
  std::uint64_t worldRevision{std::numeric_limits<std::uint64_t>::max()};
  bool initialized{};
  VisibilityGrid4D visibilityGrid;
  std::vector<FeatureEdge4D> edges;
  std::vector<VisibleBoundaryCell4D> boundaryCells;
  std::vector<Line3> visibleLines;
  Vec4 linePosition{};
  Vec4 lineImageX{};
  Vec4 lineImageY{};
  Vec4 lineImageZ{};
  Vec4 lineForward{};
  bool linesInitialized{};
  SDL_Texture *volumeTexture{};
  int volumeWidth{};
  int volumeHeight{};
  Vec4 volumePosition{};
  Vec4 volumeImageX{};
  Vec4 volumeImageY{};
  Vec4 volumeImageZ{};
  Vec4 volumeForward{};
  double volumeDisplayYaw{};
  double volumeDisplayPitch{};
  double volumeDisplayDistance{};
  int volumeDisplayWidth{};
  int volumeDisplayHeight{};
  VolumeViewMode volumeViewMode{VolumeViewMode::Solid};
  bool volumeInitialized{};
};

void reportStartupStage(const char *dimension, int radius, int fullRadius,
                        std::size_t loadedChunks, std::uint64_t startupCounter,
                        bool &initialReported, bool &fullReported) {
  if (std::getenv("PROJ4D_PROFILE_STARTUP") == nullptr) {
    return;
  }
  const double milliseconds =
      static_cast<double>(SDL_GetPerformanceCounter() - startupCounter) *
      1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
  if (!initialReported) {
    std::cerr << dimension
              << " startup profile: first_geometry=" << milliseconds
              << " ms, radius=" << radius << ", loaded_chunks=" << loadedChunks
              << '\n';
    initialReported = true;
  }
  if (radius == fullRadius && !fullReported) {
    std::cerr << dimension << " startup profile: full_geometry=" << milliseconds
              << " ms, radius=" << radius << ", loaded_chunks=" << loadedChunks
              << '\n';
    fullReported = true;
  }
}

struct InventoryUiState {
  bool open{};
  CraftingStation station{CraftingStation::Hand};
  double yaw{-0.55};
  double pitch{0.42};
};

enum class InventoryUiArea {
  Storage,
  Quick,
  Crafting,
  Output,
};

struct InventoryUiSlot {
  InventoryUiArea area{};
  std::size_t index{};
  SDL_Rect rectangle{};
  double depth{};
};

void drawCenteredBitmapText(SDL_Renderer *renderer, const std::string &text,
                            int centerX, int y, int scale,
                            const std::array<std::uint8_t, 3> &color);

enum class WorldDimension {
  Four,
  Three,
  Two,
};

constexpr std::array<WorldDimension, 3> dimensionChoices{
    WorldDimension::Four,
    WorldDimension::Three,
    WorldDimension::Two,
};

std::size_t dimensionChoiceIndex(WorldDimension dimension) {
  for (std::size_t index = 0; index < dimensionChoices.size(); ++index) {
    if (dimensionChoices[index] == dimension) {
      return index;
    }
  }
  return 0U;
}

const char *dimensionName(WorldDimension dimension) {
  switch (dimension) {
  case WorldDimension::Four:
    return "4D";
  case WorldDimension::Three:
    return "3D";
  case WorldDimension::Two:
    return "2D";
  }
  return "4D";
}

constexpr std::array<TerrainMode, 3> terrainChoices{
    TerrainMode::Flat,
    TerrainMode::Low,
    TerrainMode::Density,
};

std::size_t terrainChoiceIndex(TerrainMode mode) {
  for (std::size_t index = 0; index < terrainChoices.size(); ++index) {
    if (terrainChoices[index] == mode) {
      return index;
    }
  }
  return 0U;
}

const char *terrainModeName(TerrainMode mode) {
  switch (mode) {
  case TerrainMode::Flat:
    return "Flat";
  case TerrainMode::Low:
    return "Low";
  case TerrainMode::Density:
    return "High";
  }
  return "Flat";
}

struct PersistentWorldSession {
  std::filesystem::path path;
  std::uint64_t savedWorldRevision{};
  std::uint64_t savedInventoryRevision{};
  bool failureReported{};
};

bool initializePersistentWorld(GameSession &game,
                               PersistentWorldSession &session,
                               std::string &error) {
  BlockWorld &world = game.world();
  char *preferencePath = SDL_GetPrefPath("Proj4D", "Proj4D");
  if (preferencePath == nullptr) {
    error =
        std::string("could not locate the save directory: ") + SDL_GetError();
    return false;
  }
  session.path = std::filesystem::path(preferencePath) / "worlds" /
                 worldSaveFilename(world.terrainMode());
  SDL_free(preferencePath);

  InventoryState loadedInventory;
  const WorldLoadStatus loadStatus =
      loadWorldSave(session.path, world, error, &loadedInventory);
  if (loadStatus == WorldLoadStatus::Error) {
    return false;
  }
  if (loadStatus == WorldLoadStatus::Loaded) {
    game.replaceInventory(std::move(loadedInventory));
  }
  if (loadStatus == WorldLoadStatus::NotFound &&
      !saveWorldSave(session.path, world, error, &game.inventory())) {
    return false;
  }
  session.savedWorldRevision = world.revision();
  session.savedInventoryRevision = game.inventoryRevision();
  std::cout << (loadStatus == WorldLoadStatus::Loaded ? "Loaded " : "Created ")
            << terrainModeName(world.terrainMode()) << " world save\n";
  return true;
}

bool persistChangedWorld(const GameSession &game,
                         PersistentWorldSession &session) {
  const BlockWorld &world = game.world();
  if (world.revision() == session.savedWorldRevision &&
      game.inventoryRevision() == session.savedInventoryRevision) {
    return true;
  }
  std::string error;
  if (!saveWorldSave(session.path, world, error, &game.inventory())) {
    if (!session.failureReported) {
      std::cerr << "World save failed: " << error << '\n';
      session.failureReported = true;
    }
    return false;
  }
  session.savedWorldRevision = world.revision();
  session.savedInventoryRevision = game.inventoryRevision();
  session.failureReported = false;
  return true;
}

std::optional<ScreenPoint> projectForDisplay(Vec3 point,
                                             const DisplayCamera &camera,
                                             int width, int height) {
  const double yawCosine = std::cos(camera.yaw);
  const double yawSine = std::sin(camera.yaw);
  const Vec3 yawed{
      point.x * yawCosine + point.z * yawSine,
      point.y,
      -point.x * yawSine + point.z * yawCosine,
  };
  const double pitchCosine = std::cos(camera.pitch);
  const double pitchSine = std::sin(camera.pitch);
  const Vec3 rotated{
      yawed.x,
      yawed.y * pitchCosine - yawed.z * pitchSine,
      yawed.y * pitchSine + yawed.z * pitchCosine,
  };
  const double depth = camera.distance - rotated.z;
  if (depth <= 0.05) {
    return std::nullopt;
  }
  const double scale =
      static_cast<double>(std::min(width, height)) * 1.45 / depth;
  return ScreenPoint{
      static_cast<int>(
          std::lround(static_cast<double>(width) * 0.5 + rotated.x * scale)),
      static_cast<int>(
          std::lround(static_cast<double>(height) * 0.5 - rotated.y * scale)),
  };
}

void drawLine3(SDL_Renderer *renderer, const DisplayCamera &display, int width,
               int height, const Line3 &line,
               const std::array<std::uint8_t, 3> &color,
               std::uint8_t alpha = 255U) {
  const auto from = projectForDisplay(line.from, display, width, height);
  const auto to = projectForDisplay(line.to, display, width, height);
  if (!from || !to) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], alpha);
  SDL_RenderDrawLine(renderer, from->x, from->y, to->x, to->y);
}

void drawVisionCube(SDL_Renderer *renderer, const DisplayCamera &display,
                    int width, int height) {
  for (int mask = 0; mask < 8; ++mask) {
    const Vec3 from{
        (mask & 1) != 0 ? 1.0 : -1.0,
        (mask & 2) != 0 ? 1.0 : -1.0,
        (mask & 4) != 0 ? 1.0 : -1.0,
    };
    for (int bit = 0; bit < 3; ++bit) {
      if ((mask & (1 << bit)) != 0) {
        continue;
      }
      Vec3 to = from;
      to[static_cast<std::size_t>(bit)] = 1.0;
      drawLine3(renderer, display, width, height, {from, to, -1}, {62, 74, 91});
    }
  }
}

void drawFluidImmersion(SDL_Renderer *renderer, BlockId material, int width,
                        int height) {
  if (!isFluid(material)) {
    return;
  }
  const BlockColor color = blockColor(material);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue,
                         blockOpacity(material) / 3U);
  const SDL_Rect display{0, 0, width, height};
  SDL_RenderFillRect(renderer, &display);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void drawVisionCubeSky(SDL_Renderer *renderer, const DisplayCamera &display,
                       int width, int height) {
  std::vector<ScreenPoint> points;
  points.reserve(8U);
  for (int mask = 0; mask < 8; ++mask) {
    const auto projected = projectForDisplay({(mask & 1) != 0 ? 1.0 : -1.0,
                                              (mask & 2) != 0 ? 1.0 : -1.0,
                                              (mask & 4) != 0 ? 1.0 : -1.0},
                                             display, width, height);
    if (projected) {
      points.push_back(*projected);
    }
  }
  std::ranges::sort(
      points, [](const ScreenPoint &left, const ScreenPoint &right) {
        return left.x < right.x || (left.x == right.x && left.y < right.y);
      });
  points.erase(
      std::unique(points.begin(), points.end(),
                  [](const ScreenPoint &left, const ScreenPoint &right) {
                    return left.x == right.x && left.y == right.y;
                  }),
      points.end());
  if (points.size() < 3U) {
    return;
  }
  const auto cross2D = [](const ScreenPoint &origin, const ScreenPoint &first,
                          const ScreenPoint &second) {
    return static_cast<long long>(first.x - origin.x) * (second.y - origin.y) -
           static_cast<long long>(first.y - origin.y) * (second.x - origin.x);
  };
  std::vector<ScreenPoint> hull;
  hull.reserve(points.size() * 2U);
  for (const ScreenPoint &point : points) {
    while (hull.size() >= 2U &&
           cross2D(hull[hull.size() - 2U], hull.back(), point) <= 0) {
      hull.pop_back();
    }
    hull.push_back(point);
  }
  const std::size_t lowerSize = hull.size();
  for (auto current = points.rbegin() + 1; current != points.rend();
       ++current) {
    while (hull.size() > lowerSize &&
           cross2D(hull[hull.size() - 2U], hull.back(), *current) <= 0) {
      hull.pop_back();
    }
    hull.push_back(*current);
  }
  hull.pop_back();
  std::vector<SDL_Vertex> vertices;
  vertices.reserve(hull.size());
  for (const ScreenPoint &point : hull) {
    SDL_Vertex vertex{};
    vertex.position = {static_cast<float>(point.x),
                       static_cast<float>(point.y)};
    vertex.color = SDL_Color{skyColor.red, skyColor.green, skyColor.blue, 255};
    vertices.push_back(vertex);
  }
  std::vector<int> indices;
  for (std::size_t index = 1; index + 1 < hull.size(); ++index) {
    indices.push_back(0);
    indices.push_back(static_cast<int>(index));
    indices.push_back(static_cast<int>(index + 1));
  }
  static_cast<void>(SDL_RenderGeometry(
      renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
      indices.data(), static_cast<int>(indices.size())));
}

void drawProjectionSlice(SDL_Renderer *renderer, SDL_Texture *texture,
                         const DisplayCamera &display, int width, int height) {
  // SDL interpolates texture coordinates affinely, so use a small fixed mesh
  // to closely follow the perspective projection without regenerating the
  // high-resolution slice texture while the user orbits the vision cube.
  constexpr int subdivisions = 24;
  constexpr int rowSize = subdivisions + 1;
  std::array<SDL_Vertex, static_cast<std::size_t>(rowSize * rowSize)>
      vertices{};
  for (int y = 0; y <= subdivisions; ++y) {
    const double textureY = static_cast<double>(y) / subdivisions;
    for (int x = 0; x <= subdivisions; ++x) {
      const double textureX = static_cast<double>(x) / subdivisions;
      const auto point =
          projectForDisplay({-1.0 + textureX * 2.0, 1.0 - textureY * 2.0, 0.0},
                            display, width, height);
      if (!point) {
        return;
      }
      SDL_Vertex &vertex = vertices[static_cast<std::size_t>(y * rowSize + x)];
      vertex.position = {static_cast<float>(point->x),
                         static_cast<float>(point->y)};
      vertex.color = {255, 255, 255, 255};
      vertex.tex_coord = {static_cast<float>(textureX),
                          static_cast<float>(textureY)};
    }
  }
  std::array<int, static_cast<std::size_t>(subdivisions * subdivisions * 6)>
      indices{};
  std::size_t index = 0U;
  for (int y = 0; y < subdivisions; ++y) {
    for (int x = 0; x < subdivisions; ++x) {
      const int topLeft = y * rowSize + x;
      const int topRight = topLeft + 1;
      const int bottomLeft = topLeft + rowSize;
      const int bottomRight = bottomLeft + 1;
      indices[index++] = topLeft;
      indices[index++] = topRight;
      indices[index++] = bottomLeft;
      indices[index++] = topRight;
      indices[index++] = bottomRight;
      indices[index++] = bottomLeft;
    }
  }
  static_cast<void>(SDL_RenderGeometry(
      renderer, texture, vertices.data(), static_cast<int>(vertices.size()),
      indices.data(), static_cast<int>(indices.size())));
}

void drawCrosshair(SDL_Renderer *renderer, const DisplayCamera &display,
                   int width, int height) {
  constexpr double radius = 0.075;
  const std::array<std::array<std::uint8_t, 3>, 3> colors{{
      {255, 92, 92},
      {100, 255, 145},
      {85, 180, 255},
  }};
  for (int axis = 0; axis < 3; ++axis) {
    Vec3 from{};
    Vec3 to{};
    from[static_cast<std::size_t>(axis)] = -radius;
    to[static_cast<std::size_t>(axis)] = radius;
    drawLine3(renderer, display, width, height, {from, to, axis},
              colors[static_cast<std::size_t>(axis)]);
  }
}

std::array<std::uint8_t, 7> glyphRows(char character) {
  switch (character) {
  case '0':
    return {14, 17, 19, 21, 25, 17, 14};
  case '1':
    return {4, 12, 4, 4, 4, 4, 14};
  case '2':
    return {14, 17, 1, 2, 4, 8, 31};
  case '3':
    return {30, 1, 1, 14, 1, 1, 30};
  case '4':
    return {2, 6, 10, 18, 31, 2, 2};
  case '5':
    return {31, 16, 16, 30, 1, 1, 30};
  case '6':
    return {14, 16, 16, 30, 17, 17, 14};
  case '7':
    return {31, 1, 2, 4, 8, 8, 8};
  case '8':
    return {14, 17, 17, 14, 17, 17, 14};
  case '9':
    return {14, 17, 17, 15, 1, 1, 14};
  case 'A':
    return {14, 17, 17, 31, 17, 17, 17};
  case 'B':
    return {30, 17, 17, 30, 17, 17, 30};
  case 'C':
    return {14, 17, 16, 16, 16, 17, 14};
  case 'D':
    return {30, 17, 17, 17, 17, 17, 30};
  case 'E':
    return {31, 16, 16, 30, 16, 16, 31};
  case 'F':
    return {31, 16, 16, 30, 16, 16, 16};
  case 'G':
    return {14, 17, 16, 23, 17, 17, 15};
  case 'H':
    return {17, 17, 17, 31, 17, 17, 17};
  case 'I':
    return {14, 4, 4, 4, 4, 4, 14};
  case 'J':
    return {7, 2, 2, 2, 2, 18, 12};
  case 'K':
    return {17, 18, 20, 24, 20, 18, 17};
  case 'L':
    return {16, 16, 16, 16, 16, 16, 31};
  case 'M':
    return {17, 27, 21, 21, 17, 17, 17};
  case 'N':
    return {17, 25, 21, 19, 17, 17, 17};
  case 'O':
    return {14, 17, 17, 17, 17, 17, 14};
  case 'P':
    return {30, 17, 17, 30, 16, 16, 16};
  case 'Q':
    return {14, 17, 17, 17, 21, 18, 13};
  case 'R':
    return {30, 17, 17, 30, 20, 18, 17};
  case 'S':
    return {15, 16, 16, 14, 1, 1, 30};
  case 'T':
    return {31, 4, 4, 4, 4, 4, 4};
  case 'U':
    return {17, 17, 17, 17, 17, 17, 14};
  case 'V':
    return {17, 17, 17, 17, 17, 10, 4};
  case 'W':
    return {17, 17, 17, 21, 21, 21, 10};
  case 'X':
    return {17, 17, 10, 4, 10, 17, 17};
  case 'Y':
    return {17, 17, 10, 4, 4, 4, 4};
  case 'Z':
    return {31, 1, 2, 4, 8, 16, 31};
  case ':':
    return {0, 4, 4, 0, 4, 4, 0};
  case '.':
    return {0, 0, 0, 0, 0, 4, 4};
  case '-':
    return {0, 0, 0, 31, 0, 0, 0};
  case '|':
    return {4, 4, 4, 4, 4, 4, 4};
  default:
    return {};
  }
}

void drawBitmapText(SDL_Renderer *renderer, const std::string &text, int x,
                    int y, int scale,
                    const std::array<std::uint8_t, 3> &color) {
  SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], 255);
  for (const char character : text) {
    const auto rows = glyphRows(character);
    for (int row = 0; row < 7; ++row) {
      for (int column = 0; column < 5; ++column) {
        if ((rows[static_cast<std::size_t>(row)] &
             (1U << static_cast<unsigned int>(4 - column))) == 0U) {
          continue;
        }
        const SDL_Rect pixel{
            x + column * scale,
            y + row * scale,
            scale,
            scale,
        };
        SDL_RenderFillRect(renderer, &pixel);
      }
    }
    x += 6 * scale;
  }
}

std::pair<ScreenPoint, double>
projectInventoryPoint(int x, int y, int z, int side, double yaw, double pitch,
                      int centerX, int centerY, double scale) {
  const double middle = static_cast<double>(side - 1) * 0.5;
  const Vec3 point{static_cast<double>(x) - middle,
                   middle - static_cast<double>(y),
                   static_cast<double>(z) - middle};
  const double yawCosine = std::cos(yaw);
  const double yawSine = std::sin(yaw);
  const Vec3 yawed{point.x * yawCosine + point.z * yawSine, point.y,
                   -point.x * yawSine + point.z * yawCosine};
  const double pitchCosine = std::cos(pitch);
  const double pitchSine = std::sin(pitch);
  const Vec3 rotated{yawed.x, yawed.y * pitchCosine - yawed.z * pitchSine,
                     yawed.y * pitchSine + yawed.z * pitchCosine};
  return {{centerX + static_cast<int>(std::lround(rotated.x * scale)),
           centerY - static_cast<int>(std::lround(rotated.y * scale))},
          rotated.z};
}

void appendProjectedSlots(std::vector<InventoryUiSlot> &slots,
                          InventoryUiArea area,
                          std::span<const std::size_t> indices, int side,
                          const InventoryUiState &ui, int centerX, int centerY,
                          double scale, int size) {
  for (const std::size_t index : indices) {
    const int x = static_cast<int>(index % static_cast<std::size_t>(side));
    const int y = static_cast<int>((index / static_cast<std::size_t>(side)) %
                                   static_cast<std::size_t>(side));
    const int z =
        static_cast<int>(index / static_cast<std::size_t>(side * side));
    const auto [point, depth] = projectInventoryPoint(
        x, y, z, side, ui.yaw, ui.pitch, centerX, centerY, scale);
    slots.push_back({area,
                     index,
                     {point.x - size / 2, point.y - size / 2, size, size},
                     depth});
  }
}

std::vector<InventoryUiSlot> inventoryUiSlots(PlayDimension dimension,
                                              CraftingStation station,
                                              const InventoryUiState &ui,
                                              int width, int height) {
  std::vector<InventoryUiSlot> slots;
  const std::vector<std::size_t> storage = visibleStorageSlots(dimension);
  const std::vector<std::size_t> quick = visibleQuickSlots(dimension);
  const std::vector<std::size_t> crafting =
      visibleCraftingSlots(dimension, station);
  const int craftingSide =
      station == CraftingStation::Hand ? handCraftSide : tableCraftSide;
  if (dimension == PlayDimension::Four) {
    const double storageScale = std::max(24.0, std::min(width, height) / 12.0);
    appendProjectedSlots(slots, InventoryUiArea::Storage, storage, storageSide,
                         ui, width * 35 / 100, height * 43 / 100, storageScale,
                         22);
    appendProjectedSlots(slots, InventoryUiArea::Quick, quick, quickSide, ui,
                         width * 35 / 100, height * 78 / 100,
                         storageScale * 0.72, 20);
    appendProjectedSlots(slots, InventoryUiArea::Crafting, crafting,
                         craftingSide, ui, width * 73 / 100, height * 45 / 100,
                         storageScale * 0.88, 22);
    slots.push_back({InventoryUiArea::Output,
                     0U,
                     {width * 83 / 100 - 15, height * 70 / 100 - 15, 30, 30},
                     100.0});
    return slots;
  }

  if (dimension == PlayDimension::Three) {
    const int size = std::clamp(std::min(width, height) / 15, 30, 54);
    const int storageLeft = width / 5;
    const int storageTop = height / 4;
    for (std::size_t visible = 0; visible < storage.size(); ++visible) {
      slots.push_back({InventoryUiArea::Storage,
                       storage[visible],
                       {storageLeft + static_cast<int>(visible % 4U) * size,
                        storageTop + static_cast<int>(visible / 4U) * size,
                        size - 3, size - 3}});
    }
    const int quickTop = storageTop + size * 5;
    for (std::size_t visible = 0; visible < quick.size(); ++visible) {
      slots.push_back({InventoryUiArea::Quick,
                       quick[visible],
                       {storageLeft + static_cast<int>(visible % 3U) * size,
                        quickTop + static_cast<int>(visible / 3U) * size,
                        size - 3, size - 3}});
    }
    const int craftLeft = width * 3 / 5;
    for (std::size_t visible = 0; visible < crafting.size(); ++visible) {
      slots.push_back(
          {InventoryUiArea::Crafting,
           crafting[visible],
           {craftLeft + static_cast<int>(visible % craftingSide) * size,
            storageTop + static_cast<int>(visible / craftingSide) * size,
            size - 3, size - 3}});
    }
    slots.push_back({InventoryUiArea::Output,
                     0U,
                     {craftLeft + craftingSide * size + size / 2,
                      storageTop + craftingSide * size / 2 - size / 2, size - 3,
                      size - 3}});
    return slots;
  }

  const int slotWidth = std::clamp(width / 12, 42, 72);
  const int slotHeight = std::max(10, slotWidth / 5);
  const int storageLeft =
      (width - static_cast<int>(storage.size()) * slotWidth) / 2;
  for (std::size_t visible = 0; visible < storage.size(); ++visible) {
    slots.push_back({InventoryUiArea::Storage,
                     storage[visible],
                     {storageLeft + static_cast<int>(visible) * slotWidth,
                      height * 35 / 100, slotWidth - 4, slotHeight}});
  }
  const int quickLeft =
      (width - static_cast<int>(quick.size()) * slotWidth) / 2;
  for (std::size_t visible = 0; visible < quick.size(); ++visible) {
    slots.push_back({InventoryUiArea::Quick,
                     quick[visible],
                     {quickLeft + static_cast<int>(visible) * slotWidth,
                      height * 53 / 100, slotWidth - 4, slotHeight}});
  }
  const int craftLeft =
      (width - (static_cast<int>(crafting.size()) + 2) * slotWidth) / 2;
  for (std::size_t visible = 0; visible < crafting.size(); ++visible) {
    slots.push_back({InventoryUiArea::Crafting,
                     crafting[visible],
                     {craftLeft + static_cast<int>(visible) * slotWidth,
                      height * 70 / 100, slotWidth - 4, slotHeight}});
  }
  slots.push_back(
      {InventoryUiArea::Output,
       0U,
       {craftLeft + (static_cast<int>(crafting.size()) + 1) * slotWidth,
        height * 70 / 100, slotWidth - 4, slotHeight}});
  return slots;
}

const ItemStack &stackForUiSlot(const InventoryState &inventory,
                                const InventoryUiSlot &slot,
                                CraftingStation station,
                                const ItemStack &outputStack) {
  switch (slot.area) {
  case InventoryUiArea::Storage:
    return inventory.storage.at(slot.index);
  case InventoryUiArea::Quick:
    return inventory.quick.at(slot.index);
  case InventoryUiArea::Crafting:
    return craftingSlots(inventory, station)[slot.index];
  case InventoryUiArea::Output:
    return outputStack;
  }
  return outputStack;
}

std::string inventoryItemLabel(BlockId item) {
  std::string label(blockName(item));
  std::ranges::transform(label, label.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return label;
}

void drawInventoryTooltip(SDL_Renderer *renderer, const ItemStack &stack,
                          int mouseX, int mouseY, int width, int height) {
  if (stack.empty()) {
    return;
  }
  const std::string label = inventoryItemLabel(stack.item);
  constexpr int scale = 1;
  constexpr int padding = 5;
  const int textWidth = std::max(1, static_cast<int>(label.size()) * 6 - 1);
  const int tooltipWidth = textWidth + padding * 2;
  const int tooltipHeight = 7 * scale + padding * 2;
  const int left =
      std::clamp(mouseX + 14, 0, std::max(0, width - tooltipWidth));
  const int top =
      std::clamp(mouseY + 18, 0, std::max(0, height - tooltipHeight));
  const SDL_Rect tooltip{left, top, tooltipWidth, tooltipHeight};
  SDL_SetRenderDrawColor(renderer, 5, 9, 15, 242);
  SDL_RenderFillRect(renderer, &tooltip);
  SDL_SetRenderDrawColor(renderer, 121, 149, 181, 255);
  SDL_RenderDrawRect(renderer, &tooltip);
  drawBitmapText(renderer, label, left + padding, top + padding, scale,
                 {238, 245, 252});
}

void drawInventorySlot(SDL_Renderer *renderer, const InventoryUiSlot &slot,
                       const ItemStack &stack, PlayDimension dimension,
                       bool selected) {
  SDL_SetRenderDrawColor(renderer, selected ? 236 : 69, selected ? 245 : 83,
                         selected ? 255 : 101, 235);
  SDL_RenderDrawRect(renderer, &slot.rectangle);
  if (stack.empty()) {
    return;
  }
  const BlockColor color = blockColor(stack.item);
  SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 245);
  if (dimension == PlayDimension::Two) {
    const int x = slot.rectangle.x + slot.rectangle.w / 2;
    SDL_RenderDrawLine(renderer, x, slot.rectangle.y + 2, x,
                       slot.rectangle.y + slot.rectangle.h - 2);
  } else {
    SDL_Rect itemRect{slot.rectangle.x + 4, slot.rectangle.y + 4,
                      std::max(1, slot.rectangle.w - 8),
                      std::max(1, slot.rectangle.h - 8)};
    SDL_RenderFillRect(renderer, &itemRect);
    if (dimension == PlayDimension::Four) {
      SDL_SetRenderDrawColor(renderer, std::min(255, color.red + 35),
                             std::min(255, color.green + 35),
                             std::min(255, color.blue + 35), 255);
      SDL_RenderDrawLine(renderer, itemRect.x, itemRect.y, itemRect.x + 4,
                         itemRect.y - 4);
      SDL_RenderDrawLine(renderer, itemRect.x + itemRect.w, itemRect.y,
                         itemRect.x + itemRect.w + 4, itemRect.y - 4);
      SDL_RenderDrawLine(renderer, itemRect.x + 4, itemRect.y - 4,
                         itemRect.x + itemRect.w + 4, itemRect.y - 4);
    }
  }
  if (stack.count > 1) {
    drawBitmapText(renderer, std::to_string(stack.count), slot.rectangle.x + 2,
                   slot.rectangle.y + 2, 1, {255, 255, 255});
  }
  if (isTool(stack.item)) {
    const int maximum = std::max(1, maximumDurability(stack.item));
    const int barWidth = std::max(1, slot.rectangle.w - 4);
    const int remaining =
        std::clamp(barWidth * stack.durability / maximum, 0, barWidth);
    SDL_SetRenderDrawColor(renderer, 31, 38, 47, 255);
    SDL_RenderDrawLine(renderer, slot.rectangle.x + 2,
                       slot.rectangle.y + slot.rectangle.h - 3,
                       slot.rectangle.x + 1 + barWidth,
                       slot.rectangle.y + slot.rectangle.h - 3);
    SDL_SetRenderDrawColor(renderer, 100, 225, 145, 255);
    SDL_RenderDrawLine(renderer, slot.rectangle.x + 2,
                       slot.rectangle.y + slot.rectangle.h - 3,
                       slot.rectangle.x + 1 + remaining,
                       slot.rectangle.y + slot.rectangle.h - 3);
  }
}

void drawInventoryOverlay(SDL_Renderer *renderer,
                          const InventoryState &inventory,
                          PlayDimension dimension, const InventoryUiState &ui,
                          int width, int height) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 4, 7, 12, 218);
  const SDL_Rect panel{width / 12, height / 10, width * 5 / 6, height * 4 / 5};
  SDL_RenderFillRect(renderer, &panel);
  drawCenteredBitmapText(renderer,
                         dimension == PlayDimension::Four
                             ? "4D INVENTORY CUBE"
                             : (dimension == PlayDimension::Three
                                    ? "3D INVENTORY"
                                    : "2D INVENTORY LINE"),
                         width / 2, panel.y + 18, 2, {226, 238, 250});
  drawCenteredBitmapText(renderer,
                         ui.station == CraftingStation::Hand ? "HAND CRAFTING"
                                                             : "TABLE CRAFTING",
                         width * 73 / 100, panel.y + 48, 1, {126, 215, 174});
  std::vector<InventoryUiSlot> slots =
      inventoryUiSlots(dimension, ui.station, ui, width, height);
  std::ranges::sort(slots, std::less{}, &InventoryUiSlot::depth);
  const ItemStack outputStack =
      craftingOutput(inventory, dimension, ui.station);
  for (const InventoryUiSlot &slot : slots) {
    const bool selected =
        slot.area == InventoryUiArea::Quick &&
        static_cast<int>(slot.index) == inventory.selectedQuick;
    drawInventorySlot(renderer, slot,
                      stackForUiSlot(inventory, slot, ui.station, outputStack),
                      dimension, selected);
  }
  int mouseX = 0;
  int mouseY = 0;
  SDL_GetMouseState(&mouseX, &mouseY);
  const SDL_Point mousePoint{mouseX, mouseY};
  const ItemStack *hoveredStack = nullptr;
  for (auto slot = slots.rbegin(); slot != slots.rend(); ++slot) {
    if (SDL_PointInRect(&mousePoint, &slot->rectangle) == SDL_FALSE) {
      continue;
    }
    const ItemStack &stack =
        stackForUiSlot(inventory, *slot, ui.station, outputStack);
    if (!stack.empty()) {
      hoveredStack = &stack;
    }
    break;
  }
  if (!inventory.cursor.empty()) {
    const InventoryUiSlot cursorSlot{
        InventoryUiArea::Storage, 0U, {mouseX - 12, mouseY - 12, 24, 24}};
    drawInventorySlot(renderer, cursorSlot, inventory.cursor, dimension, false);
  }
  if (hoveredStack != nullptr) {
    drawInventoryTooltip(renderer, *hoveredStack, mouseX, mouseY, width,
                         height);
  }
  drawCenteredBitmapText(renderer,
                         dimension == PlayDimension::Four
                             ? "T CLOSE  RIGHT CLICK ITEMS  RIGHT DRAG ROTATES"
                             : "T CLOSE  LEFT AND RIGHT CLICK ITEMS",
                         width / 2, panel.y + panel.h - 28, 1, {139, 160, 181});
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

bool applyInventoryUiClick(GameSession &game, PlayDimension dimension,
                           const InventoryUiState &ui, int width, int height,
                           int mouseX, int mouseY, int button) {
  std::vector<InventoryUiSlot> slots =
      inventoryUiSlots(dimension, ui.station, ui, width, height);
  std::ranges::sort(slots, std::greater{}, &InventoryUiSlot::depth);
  InventoryState changed = game.inventory();
  const SDL_Point mousePoint{mouseX, mouseY};
  for (const InventoryUiSlot &slot : slots) {
    if (SDL_PointInRect(&mousePoint, &slot.rectangle) == SDL_FALSE) {
      continue;
    }
    if (slot.area == InventoryUiArea::Output) {
      if (!takeCraftingOutput(changed, dimension, ui.station)) {
        return false;
      }
    } else {
      ItemStack *target = nullptr;
      if (slot.area == InventoryUiArea::Storage) {
        target = &changed.storage.at(slot.index);
      } else if (slot.area == InventoryUiArea::Quick) {
        target = &changed.quick.at(slot.index);
      } else {
        target = &craftingSlots(changed, ui.station)[slot.index];
      }
      clickStack(*target, changed.cursor, button);
    }
    game.replaceInventory(std::move(changed));
    return true;
  }
  return false;
}

void pointAtInventorySmokeItem(SDL_Window *window, SDL_Renderer *renderer,
                               const InventoryState &inventory,
                               PlayDimension dimension,
                               const InventoryUiState &ui) {
  int width = 1;
  int height = 1;
  SDL_GetRendererOutputSize(renderer, &width, &height);
  std::vector<InventoryUiSlot> slots =
      inventoryUiSlots(dimension, ui.station, ui, width, height);
  std::ranges::sort(slots, std::greater{}, &InventoryUiSlot::depth);
  const ItemStack outputStack =
      craftingOutput(inventory, dimension, ui.station);
  for (const InventoryUiSlot &slot : slots) {
    if (stackForUiSlot(inventory, slot, ui.station, outputStack).empty()) {
      continue;
    }
    SDL_WarpMouseInWindow(window, slot.rectangle.x + slot.rectangle.w / 2,
                          slot.rectangle.y + slot.rectangle.h / 2);
    return;
  }
}

void drawQuickStorage(SDL_Renderer *renderer, const InventoryState &inventory,
                      PlayDimension dimension, int width, int height,
                      int fourDLayer = -1, bool awaitingFourDSlot = false) {
  std::vector<std::size_t> visible = visibleQuickSlots(dimension);
  if (dimension == PlayDimension::Four) {
    fourDLayer =
        std::clamp(fourDLayer >= 0 ? fourDLayer
                                   : std::clamp(inventory.selectedQuick, 0,
                                                quickSlotCount - 1) /
                                         fourDQuickLayerSlotCount,
                   0, quickSide - 1);
    visible.clear();
    visible.reserve(fourDQuickLayerSlotCount);
    const std::size_t first =
        static_cast<std::size_t>(fourDLayer * fourDQuickLayerSlotCount);
    for (int slot = 0; slot < fourDQuickLayerSlotCount; ++slot) {
      visible.push_back(first + static_cast<std::size_t>(slot));
    }
  }
  const int size = dimension == PlayDimension::Four ? 24 : 30;
  const int shown = static_cast<int>(visible.size());
  const int columns = dimension == PlayDimension::Four ? quickSide : shown;
  const int rows = (shown + columns - 1) / columns;
  const int left = (width - columns * size) / 2;
  const int top = height - rows * size - 14;
  if (dimension == PlayDimension::Four) {
    const std::string prompt =
        awaitingFourDSlot
            ? "LAYER " + std::to_string(fourDLayer + 1) + "  PRESS 1-9"
            : "PRESS 1-3 THEN 1-9";
    drawCenteredBitmapText(renderer, prompt, width / 2, top - 13, 1,
                           {224, 236, 248});
  }
  for (int visibleIndex = 0; visibleIndex < shown; ++visibleIndex) {
    const std::size_t index = visible[static_cast<std::size_t>(visibleIndex)];
    const InventoryUiSlot slot{InventoryUiArea::Quick,
                               index,
                               {left + (visibleIndex % columns) * size,
                                top + (visibleIndex / columns) * size, size - 2,
                                size - 2}};
    drawInventorySlot(renderer, slot, inventory.quick[index], dimension,
                      static_cast<int>(index) == inventory.selectedQuick);
    if (dimension == PlayDimension::Four) {
      drawBitmapText(renderer, std::to_string(visibleIndex + 1),
                     slot.rectangle.x + slot.rectangle.w - 6,
                     slot.rectangle.y + slot.rectangle.h - 8, 1,
                     {235, 242, 250});
    }
  }
}

void drawStatusBar(SDL_Renderer *renderer, const Camera4D &camera, int width,
                   VolumeViewMode viewMode) {
  const int scale = width >= 820 ? 2 : 1;
  const int barHeight = 9 * scale + 4;
  const SDL_Rect bar{0, 0, width, barHeight};
  SDL_SetRenderDrawColor(renderer, 9, 15, 24, 255);
  SDL_RenderFillRect(renderer, &bar);
  SDL_SetRenderDrawColor(renderer, 55, 78, 102, 255);
  SDL_RenderDrawLine(renderer, 0, barHeight - 1, width, barHeight - 1);
  const std::string status =
      formatViewStatus(camera) + (viewMode == VolumeViewMode::CenterSlice
                                      ? " | VIEW:SLICE"
                                      : " | VIEW:SOLID");
  drawBitmapText(renderer, status, 4 * scale, 2 * scale, scale,
                 {224, 236, 248});
}

std::string formatViewStatus(const Camera2D &camera) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << "X:" << camera.position.x
         << " Y:" << camera.position.y << " | V:" << std::setprecision(1)
         << camera.verticalPitch() * 180.0 / pi
         << " DIR:" << (camera.horizontalDirection() > 0 ? "R" : "L");
  return output.str();
}

void drawStatusBar(SDL_Renderer *renderer, const Camera2D &camera, int width) {
  const int scale = width >= 820 ? 2 : 1;
  const int barHeight = 9 * scale + 4;
  const SDL_Rect bar{0, 0, width, barHeight};
  SDL_SetRenderDrawColor(renderer, 9, 15, 24, 255);
  SDL_RenderFillRect(renderer, &bar);
  SDL_SetRenderDrawColor(renderer, 55, 78, 102, 255);
  SDL_RenderDrawLine(renderer, 0, barHeight - 1, width, barHeight - 1);
  drawBitmapText(renderer, formatViewStatus(camera), 4 * scale, 2 * scale,
                 scale, {224, 236, 248});
}

std::string formatViewStatus(const Camera3D &camera) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << "X:" << camera.position.x
         << " Y:" << camera.position.y << " Z:" << camera.position.z
         << " | H:" << std::setprecision(1)
         << camera.horizontalYaw() * 180.0 / pi
         << " V:" << camera.verticalPitch() * 180.0 / pi;
  return output.str();
}

void drawStatusBar(SDL_Renderer *renderer, const Camera3D &camera, int width) {
  const int scale = width >= 820 ? 2 : 1;
  const int barHeight = 9 * scale + 4;
  const SDL_Rect bar{0, 0, width, barHeight};
  SDL_SetRenderDrawColor(renderer, 9, 15, 24, 255);
  SDL_RenderFillRect(renderer, &bar);
  SDL_SetRenderDrawColor(renderer, 55, 78, 102, 255);
  SDL_RenderDrawLine(renderer, 0, barHeight - 1, width, barHeight - 1);
  drawBitmapText(renderer, formatViewStatus(camera), 4 * scale, 2 * scale,
                 scale, {224, 236, 248});
}

void drawCenteredBitmapText(SDL_Renderer *renderer, const std::string &text,
                            int centerX, int y, int scale,
                            const std::array<std::uint8_t, 3> &color) {
  const int width =
      std::max(0, static_cast<int>(text.size()) * 6 * scale - scale);
  drawBitmapText(renderer, text, centerX - width / 2, y, scale, color);
}

SDL_Rect terrainChoiceRect(int width, int height, TerrainMode mode) {
  const int buttonWidth = std::clamp(width - 80, 240, 440);
  constexpr int buttonHeight = 72;
  constexpr int buttonSpacing = 86;
  const int y = height / 2 - 124 +
                static_cast<int>(terrainChoiceIndex(mode)) * buttonSpacing;
  return {
      (width - buttonWidth) / 2,
      y,
      buttonWidth,
      buttonHeight,
  };
}

SDL_Rect dimensionChoiceRect(int width, int height, WorldDimension dimension) {
  const int buttonWidth = std::clamp(width - 80, 240, 440);
  constexpr int buttonHeight = 80;
  constexpr int buttonSpacing = 94;
  const int index = static_cast<int>(dimensionChoiceIndex(dimension));
  return {(width - buttonWidth) / 2, height / 2 - 132 + index * buttonSpacing,
          buttonWidth, buttonHeight};
}

bool containsPoint(const SDL_Rect &rectangle, int x, int y) {
  return x >= rectangle.x && x < rectangle.x + rectangle.w &&
         y >= rectangle.y && y < rectangle.y + rectangle.h;
}

void drawMenuButton(SDL_Renderer *renderer, const SDL_Rect &rectangle,
                    const std::string &label, const std::string &description,
                    bool selected) {
  if (selected) {
    SDL_SetRenderDrawColor(renderer, 24, 54, 72, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 12, 23, 35, 255);
  }
  SDL_RenderFillRect(renderer, &rectangle);
  if (selected) {
    SDL_SetRenderDrawColor(renderer, 104, 242, 175, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 67, 91, 116, 255);
  }
  SDL_RenderDrawRect(renderer, &rectangle);

  const int centerX = rectangle.x + rectangle.w / 2;
  drawCenteredBitmapText(renderer, label, centerX, rectangle.y + 9, 3,
                         selected ? std::array<std::uint8_t, 3>{235, 255, 246}
                                  : std::array<std::uint8_t, 3>{188, 205, 220});
  drawCenteredBitmapText(renderer, description, centerX, rectangle.y + 51, 1,
                         {145, 174, 198});
}

void drawTerrainChoice(SDL_Renderer *renderer, const SDL_Rect &rectangle,
                       TerrainMode mode, WorldDimension dimension,
                       bool selected) {
  std::string label;
  std::string description;
  switch (mode) {
  case TerrainMode::Flat:
    label = "FLAT";
    description =
        "INFINITE " + std::string(dimensionName(dimension)) + " SUPERFLAT Y 0";
    break;
  case TerrainMode::Low:
    label = "LOW";
    description =
        std::string(dimensionName(dimension)) + " HYPERCRAFT FLAT TERRAIN";
    break;
  case TerrainMode::Density:
    label = "HIGH";
    description =
        dimension == WorldDimension::Four
            ? "ORIGINAL 4D DENSITY TERRAIN"
            : std::string(dimensionName(dimension)) + " SLICE OF 4D DENSITY";
    break;
  }
  drawMenuButton(renderer, rectangle, label, description, selected);
}

void drawDimensionMenu(SDL_Renderer *renderer, WorldDimension selected,
                       int width, int height) {
  SDL_SetRenderDrawColor(renderer, 3, 7, 13, 255);
  SDL_RenderClear(renderer);
  const int titleScale = width >= 700 ? 4 : 2;
  drawCenteredBitmapText(renderer, "SELECT DIMENSION", width / 2,
                         std::max(32, height / 7), titleScale, {226, 238, 250});
  drawCenteredBitmapText(renderer, "CHOOSE WORLD SPACE", width / 2,
                         std::max(80, height / 7 + 52), 2, {104, 242, 175});
  drawMenuButton(
      renderer, dimensionChoiceRect(width, height, WorldDimension::Four), "4D",
      "4D WORLD TO 3D VISION CUBE", selected == WorldDimension::Four);
  drawMenuButton(
      renderer, dimensionChoiceRect(width, height, WorldDimension::Three), "3D",
      "W 0 SLICE TO 2D VIEW", selected == WorldDimension::Three);
  drawMenuButton(renderer,
                 dimensionChoiceRect(width, height, WorldDimension::Two), "2D",
                 "2D WORLD TO 1D VISION LINE", selected == WorldDimension::Two);
  drawCenteredBitmapText(renderer, "ARROWS ENTER 4 3 2 OR CLICK", width / 2,
                         height - 54, 2, {126, 151, 174});
  SDL_RenderPresent(renderer);
}

void drawTerrainMenu(SDL_Renderer *renderer, WorldDimension dimension,
                     TerrainMode selected, int width, int height) {
  SDL_SetRenderDrawColor(renderer, 3, 7, 13, 255);
  SDL_RenderClear(renderer);

  const int titleScale = width >= 700 ? 4 : 2;
  drawCenteredBitmapText(
      renderer, "SELECT " + std::string(dimensionName(dimension)) + " WORLD",
      width / 2, std::max(32, height / 7), titleScale, {226, 238, 250});
  drawCenteredBitmapText(renderer, "CHOOSE TERRAIN", width / 2,
                         std::max(80, height / 7 + 52), 2, {104, 242, 175});

  for (const TerrainMode mode : terrainChoices) {
    drawTerrainChoice(renderer, terrainChoiceRect(width, height, mode), mode,
                      dimension, selected == mode);
  }

  const SDL_Rect highButton =
      terrainChoiceRect(width, height, TerrainMode::Density);
  drawCenteredBitmapText(
      renderer, "ARROWS ENTER OR CLICK", width / 2,
      std::max(height - 54, highButton.y + highButton.h + 24), 2,
      {126, 151, 174});
  SDL_RenderPresent(renderer);
}

std::optional<WorldDimension> chooseWorldDimension(SDL_Renderer *renderer) {
  WorldDimension selected = WorldDimension::Four;
  while (true) {
    int width = 1;
    int height = 1;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        return std::nullopt;
      }
      if (event.type == SDL_KEYDOWN) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_UP || key == SDLK_LEFT) {
          const std::size_t index = dimensionChoiceIndex(selected);
          selected = dimensionChoices[(index + dimensionChoices.size() - 1U) %
                                      dimensionChoices.size()];
        } else if (key == SDLK_DOWN || key == SDLK_RIGHT || key == SDLK_TAB) {
          selected = dimensionChoices[(dimensionChoiceIndex(selected) + 1U) %
                                      dimensionChoices.size()];
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
          return selected;
        } else if (key == SDLK_4) {
          return WorldDimension::Four;
        } else if (key == SDLK_3) {
          return WorldDimension::Three;
        } else if (key == SDLK_2) {
          return WorldDimension::Two;
        }
      } else if (event.type == SDL_MOUSEMOTION) {
        for (const WorldDimension dimension : dimensionChoices) {
          if (containsPoint(dimensionChoiceRect(width, height, dimension),
                            event.motion.x, event.motion.y)) {
            selected = dimension;
          }
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT) {
        for (const WorldDimension dimension : dimensionChoices) {
          if (containsPoint(dimensionChoiceRect(width, height, dimension),
                            event.button.x, event.button.y)) {
            return dimension;
          }
        }
      }
    }
    drawDimensionMenu(renderer, selected, width, height);
    SDL_Delay(8U);
  }
}

std::optional<TerrainMode> chooseTerrainMode(SDL_Renderer *renderer,
                                             WorldDimension dimension) {
  TerrainMode selected = TerrainMode::Flat;
  while (true) {
    int width = 1;
    int height = 1;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    std::array<SDL_Rect, terrainChoices.size()> rectangles{};
    for (std::size_t index = 0; index < terrainChoices.size(); ++index) {
      rectangles[index] =
          terrainChoiceRect(width, height, terrainChoices[index]);
    }

    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        return std::nullopt;
      }
      if (event.type == SDL_KEYDOWN) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_UP || key == SDLK_LEFT) {
          const std::size_t index = terrainChoiceIndex(selected);
          selected = terrainChoices[(index + terrainChoices.size() - 1U) %
                                    terrainChoices.size()];
        } else if (key == SDLK_DOWN || key == SDLK_RIGHT || key == SDLK_TAB) {
          selected = terrainChoices[(terrainChoiceIndex(selected) + 1U) %
                                    terrainChoices.size()];
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
          return selected;
        } else if (key == SDLK_f) {
          return TerrainMode::Flat;
        } else if (key == SDLK_l) {
          return TerrainMode::Low;
        } else if (key == SDLK_h || key == SDLK_n) {
          return TerrainMode::Density;
        }
      } else if (event.type == SDL_MOUSEMOTION) {
        for (std::size_t index = 0; index < terrainChoices.size(); ++index) {
          if (containsPoint(rectangles[index], event.motion.x,
                            event.motion.y)) {
            selected = terrainChoices[index];
          }
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT) {
        for (std::size_t index = 0; index < terrainChoices.size(); ++index) {
          if (containsPoint(rectangles[index], event.button.x,
                            event.button.y)) {
            return terrainChoices[index];
          }
        }
      }
    }
    drawTerrainMenu(renderer, dimension, selected, width, height);
    SDL_Delay(8U);
  }
}

void render(SDL_Renderer *renderer, const BlockWorld &world,
            const Camera4D &camera, const DisplayCamera &display, int width,
            int height, VolumeViewMode viewMode,
            FeatureGeometryCache &geometryCache) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  const bool profile = std::getenv("PROJ4D_PROFILE_4D") != nullptr;
  const std::uint64_t frameStart = SDL_GetPerformanceCounter();
  std::uint64_t topologyDone = frameStart;
  const BlockCoord center = containingBlock(camera.position);
  constexpr int initialRadius = 3;
  if (!geometryCache.initialized || geometryCache.center != center ||
      geometryCache.worldRevision != world.revision() ||
      geometryCache.radius < renderBlockRadius) {
    const int requestedRadius =
        geometryCache.initialized ? renderBlockRadius : initialRadius;
    const bool ready = world.prepareRegion(center, requestedRadius);
    if (!geometryCache.initialized) {
      static_cast<void>(world.prepareRegion(center, renderBlockRadius));
    }
    if (ready) {
      const bool mayReuse = geometryCache.initialized &&
                            geometryCache.worldRevision == world.revision();
      geometryCache.visibilityGrid = buildVisibilityGrid4D(
          world, center, requestedRadius,
          mayReuse ? &geometryCache.visibilityGrid : nullptr);
      geometryCache.center = center;
      geometryCache.radius = requestedRadius;
      geometryCache.worldRevision = world.revision();
      geometryCache.boundaryCells = buildVisibleBoundaryCells(
          geometryCache.visibilityGrid, center, requestedRadius);
      geometryCache.edges = buildFeatureEdges(geometryCache.boundaryCells);
      geometryCache.initialized = true;
      geometryCache.volumeInitialized = false;
      geometryCache.linesInitialized = false;
      topologyDone = SDL_GetPerformanceCounter();
      reportStartupStage("4D", requestedRadius, renderBlockRadius,
                         world.loadedChunkCount(), geometryCache.startupCounter,
                         geometryCache.initialStartupReported,
                         geometryCache.fullStartupReported);
    }
  }
  const auto sameVector = [](const Vec4 &left, const Vec4 &right) {
    return left.x == right.x && left.y == right.y && left.z == right.z &&
           left.w == right.w;
  };
  const bool volumeChanged =
      geometryCache.initialized &&
      (!geometryCache.volumeInitialized ||
       !sameVector(geometryCache.volumePosition, camera.position) ||
       !sameVector(geometryCache.volumeImageX, camera.imageX) ||
       !sameVector(geometryCache.volumeImageY, camera.imageY) ||
       !sameVector(geometryCache.volumeImageZ, camera.imageZ) ||
       !sameVector(geometryCache.volumeForward, camera.forward) ||
       geometryCache.volumeViewMode != viewMode ||
       (viewMode == VolumeViewMode::Solid &&
        (geometryCache.volumeDisplayYaw != display.yaw ||
         geometryCache.volumeDisplayPitch != display.pitch ||
         geometryCache.volumeDisplayDistance != display.distance ||
         geometryCache.volumeDisplayWidth != width ||
         geometryCache.volumeDisplayHeight != height)) ||
       (viewMode == VolumeViewMode::CenterSlice &&
        geometryCache.volumeWidth !=
            std::min({width, height, maximumSliceImageWidth})));
  if (volumeChanged) {
    const std::uint64_t volumeStart = SDL_GetPerformanceCounter();
    std::vector<ProjectedBoundaryVolume4D> volumes =
        projectBoundaryVolumes4D(geometryCache.boundaryCells, camera);
    std::vector<ProjectedBoundaryVolume4D> cloudVolumes =
        projectCloudVolumes4D(visibleClouds4D(camera.position), camera);
    const std::size_t available =
        maximumProjectedBoundaryVolumes > volumes.size()
            ? maximumProjectedBoundaryVolumes - volumes.size()
            : 0U;
    if (cloudVolumes.size() > available) {
      cloudVolumes.resize(available);
    }
    volumes.insert(volumes.end(), cloudVolumes.begin(), cloudVolumes.end());
    const AveragedVolumeImage volumeImage = renderAveragedVolume4D(
        volumes, {display.yaw, display.pitch, display.distance}, width, height,
        viewMode == VolumeViewMode::CenterSlice ? maximumSliceImageWidth
                                                : maximumVolumeImageWidth,
        geometryCache.visibilityGrid.blockAt(containingBlock(camera.position)),
        viewMode);
    if (geometryCache.volumeTexture == nullptr ||
        geometryCache.volumeWidth != volumeImage.width ||
        geometryCache.volumeHeight != volumeImage.height) {
      SDL_DestroyTexture(geometryCache.volumeTexture);
      geometryCache.volumeTexture = SDL_CreateTexture(
          renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
          volumeImage.width, volumeImage.height);
      if (geometryCache.volumeTexture != nullptr) {
        SDL_SetTextureBlendMode(geometryCache.volumeTexture,
                                SDL_BLENDMODE_BLEND);
      }
      geometryCache.volumeWidth = volumeImage.width;
      geometryCache.volumeHeight = volumeImage.height;
    }
    if (geometryCache.volumeTexture != nullptr) {
      SDL_UpdateTexture(geometryCache.volumeTexture, nullptr,
                        volumeImage.rgba.data(), volumeImage.width * 4);
    }
    geometryCache.volumePosition = camera.position;
    geometryCache.volumeImageX = camera.imageX;
    geometryCache.volumeImageY = camera.imageY;
    geometryCache.volumeImageZ = camera.imageZ;
    geometryCache.volumeForward = camera.forward;
    geometryCache.volumeDisplayYaw = display.yaw;
    geometryCache.volumeDisplayPitch = display.pitch;
    geometryCache.volumeDisplayDistance = display.distance;
    geometryCache.volumeDisplayWidth = width;
    geometryCache.volumeDisplayHeight = height;
    geometryCache.volumeViewMode = viewMode;
    geometryCache.volumeInitialized = true;
    if (profile) {
      const double frequency =
          static_cast<double>(SDL_GetPerformanceFrequency());
      const double topologyMilliseconds =
          static_cast<double>(topologyDone - frameStart) * 1000.0 / frequency;
      const double volumeMilliseconds =
          static_cast<double>(SDL_GetPerformanceCounter() - volumeStart) *
          1000.0 / frequency;
      std::cerr << "4D profile: topology=" << topologyMilliseconds
                << " ms, volume=" << volumeMilliseconds
                << " ms, cells=" << geometryCache.boundaryCells.size()
                << ", volumes=" << volumes.size() << '\n';
    }
  }
  if (viewMode == VolumeViewMode::Solid) {
    drawVisionCubeSky(renderer, display, width, height);
  }
  if (geometryCache.volumeTexture != nullptr) {
    if (viewMode == VolumeViewMode::CenterSlice) {
      drawProjectionSlice(renderer, geometryCache.volumeTexture, display, width,
                          height);
    } else {
      const SDL_Rect destination{0, 0, width, height};
      SDL_RenderCopy(renderer, geometryCache.volumeTexture, nullptr,
                     &destination);
    }
  }
  drawVisionCube(renderer, display, width, height);
  const bool linesChanged =
      geometryCache.initialized &&
      (!geometryCache.linesInitialized ||
       !sameVector(geometryCache.linePosition, camera.position) ||
       !sameVector(geometryCache.lineImageX, camera.imageX) ||
       !sameVector(geometryCache.lineImageY, camera.imageY) ||
       !sameVector(geometryCache.lineImageZ, camera.imageZ) ||
       !sameVector(geometryCache.lineForward, camera.forward));
  if (linesChanged) {
    const std::uint64_t lineStart = SDL_GetPerformanceCounter();
    geometryCache.visibleLines = projectVisibleFeatureEdges(
        geometryCache.visibilityGrid, geometryCache.edges, camera);
    if (const auto hit = raycast(world, camera.position, camera.forward, 8.0)) {
      std::vector<Line3> selection = buildVisibleTesseractWireframe(
          geometryCache.visibilityGrid, hit->block, camera);
      for (Line3 &line : selection) {
        line.material = BlockId::air;
      }
      geometryCache.visibleLines.insert(geometryCache.visibleLines.end(),
                                        selection.begin(), selection.end());
    }
    geometryCache.linePosition = camera.position;
    geometryCache.lineImageX = camera.imageX;
    geometryCache.lineImageY = camera.imageY;
    geometryCache.lineImageZ = camera.imageZ;
    geometryCache.lineForward = camera.forward;
    geometryCache.linesInitialized = true;
    if (profile) {
      const double milliseconds =
          static_cast<double>(SDL_GetPerformanceCounter() - lineStart) *
          1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
      std::cerr << "4D profile: edges=" << milliseconds
                << " ms, source_edges=" << geometryCache.edges.size()
                << ", visible_fragments=" << geometryCache.visibleLines.size()
                << '\n';
    }
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const Line3 &line : geometryCache.visibleLines) {
    drawLine3(renderer, display, width, height, line,
              line.material == BlockId::air
                  ? std::array<std::uint8_t, 3>{255, 255, 255}
                  : blockColor(line.material).array(),
              line.material == BlockId::air ? 255U
                                            : blockOpacity(line.material));
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  drawCrosshair(renderer, display, width, height);
  drawStatusBar(renderer, camera, width, viewMode);
}

void render(SDL_Renderer *renderer, const BlockWorld2D &world,
            const Camera2D &camera, int width, int height,
            struct VisionGeometry2D &vision);

struct VisionGeometry2D {
  std::uint64_t startupCounter{SDL_GetPerformanceCounter()};
  bool initialStartupReported{};
  bool fullStartupReported{};
  BlockCoord2D center{};
  int radius{-1};
  std::uint64_t worldRevision{std::numeric_limits<std::uint64_t>::max()};
  bool initialized{};
  std::unordered_map<ChunkCoord, std::vector<VisibleEdge2D>, ChunkCoordHash>
      chunkEdges;
  std::vector<VisibleEdge2D> edges;
};

std::vector<ChunkCoord> chunksFor2D(const BlockCoord2D &center, int radius) {
  const ChunkCoord first =
      chunkCoordForBlock({center.x - radius, center.y - radius, 0, 0});
  const ChunkCoord last =
      chunkCoordForBlock({center.x + radius, center.y + radius, 0, 0});
  std::vector<ChunkCoord> chunks;
  for (int y = first.y; y <= last.y; ++y) {
    for (int x = first.x; x <= last.x; ++x) {
      chunks.push_back({x, y, 0, 0});
    }
  }
  return chunks;
}

bool chunksTouch2D(const ChunkCoord &left, const ChunkCoord &right) {
  return left.w == right.w && left.z == right.z &&
         std::abs(left.x - right.x) + std::abs(left.y - right.y) == 1;
}

void render(SDL_Renderer *renderer, const BlockWorld2D &world,
            const Camera2D &camera, int width, int height,
            VisionGeometry2D &vision) {
  SDL_SetRenderDrawColor(renderer, skyColor.red, skyColor.green, skyColor.blue,
                         255);
  SDL_RenderClear(renderer);

  const int stripWidth = std::max(1, height / visionLineWidthDivisor);
  const int stripLeft = (width - stripWidth) / 2;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 235, 242, 250, 72);
  for (const Cloud2D &cloud : visibleClouds2D(camera.position)) {
    double minimum = 1.0;
    double maximum = -1.0;
    bool complete = true;
    for (int mask = 0; mask < 4; ++mask) {
      const Vec2 point{cloud.minimum.x + ((mask & 1) != 0 ? cloud.size : 0.0),
                       cloud.minimum.y + ((mask & 2) != 0 ? cloud.size : 0.0)};
      const auto projected = camera.project(point);
      if (!projected) {
        complete = false;
        break;
      }
      minimum = std::min(minimum, projected->position);
      maximum = std::max(maximum, projected->position);
    }
    if (!complete || maximum < -1.0 || minimum > 1.0) {
      continue;
    }
    minimum = std::clamp(minimum, -1.0, 1.0);
    maximum = std::clamp(maximum, -1.0, 1.0);
    const int top =
        static_cast<int>(std::floor((1.0 - maximum) * 0.5 * height));
    const int bottom =
        static_cast<int>(std::ceil((1.0 - minimum) * 0.5 * height));
    const SDL_Rect cloudStrip{stripLeft, top, stripWidth,
                              std::max(1, bottom - top)};
    SDL_RenderFillRect(renderer, &cloudStrip);
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

  const BlockCoord2D center = containingBlock(camera.position);
  constexpr int initialRadius = 8;
  if (!vision.initialized || vision.center != center ||
      vision.worldRevision != world.revision() ||
      vision.radius < terrainRenderDistance2D) {
    const int requestedRadius =
        vision.initialized ? terrainRenderDistance2D : initialRadius;
    const std::vector<ChunkCoord> visibleChunks =
        chunksFor2D(center, requestedRadius);
    const bool ready = world.prepareChunks(visibleChunks);
    if (!vision.initialized) {
      static_cast<void>(
          world.prepareChunks(chunksFor2D(center, terrainRenderDistance2D)));
    }
    if (ready) {
      if (vision.worldRevision != world.revision()) {
        vision.chunkEdges.clear();
      }
      std::vector<ChunkCoord> newlyVisible;
      for (const ChunkCoord chunk : visibleChunks) {
        if (!vision.chunkEdges.contains(chunk)) {
          newlyVisible.push_back(chunk);
        }
      }
      std::unordered_map<ChunkCoord, std::vector<VisibleEdge2D>, ChunkCoordHash>
          retained;
      retained.reserve(visibleChunks.size());
      for (const ChunkCoord chunk : visibleChunks) {
        const bool touchesNewChunk =
            std::ranges::any_of(newlyVisible, [&](const ChunkCoord &added) {
              return chunksTouch2D(chunk, added);
            });
        if (auto existing = vision.chunkEdges.find(chunk);
            existing != vision.chunkEdges.end() && !touchesNewChunk) {
          retained.emplace(chunk, std::move(existing->second));
        } else {
          retained.emplace(chunk, buildVisibleEdges2DChunk(world, chunk));
        }
      }
      vision.chunkEdges = std::move(retained);
      vision.edges.clear();
      const int minimumX = center.x - requestedRadius;
      const int maximumX = center.x + requestedRadius;
      const int minimumY = center.y - requestedRadius;
      const int maximumY = center.y + requestedRadius;
      for (const auto &[chunk, edges] : vision.chunkEdges) {
        static_cast<void>(chunk);
        for (const VisibleEdge2D &edge : edges) {
          if (edge.block.x >= minimumX && edge.block.x <= maximumX &&
              edge.block.y >= minimumY && edge.block.y <= maximumY) {
            vision.edges.push_back(edge);
          }
        }
      }
      vision.center = center;
      vision.radius = requestedRadius;
      vision.worldRevision = world.revision();
      vision.initialized = true;
      reportStartupStage("2D", requestedRadius, terrainRenderDistance2D,
                         world.loadedChunkCount(), vision.startupCounter,
                         vision.initialStartupReported,
                         vision.fullStartupReported);
    }
  }
  const auto target = raycast(world, camera.position, camera.forward(), 8.0);
  const std::vector<VisionSample2D> samples = rasterizeVisionEdges2D(
      vision.edges, camera, height,
      target ? std::optional(target->block) : std::nullopt);
  int runStart = 0;
  while (runStart < height) {
    const VisionSample2D &sample =
        samples[static_cast<std::size_t>(height - 1 - runStart)];
    int runEnd = runStart + 1;
    while (runEnd < height) {
      const VisionSample2D &next =
          samples[static_cast<std::size_t>(height - 1 - runEnd)];
      if (next.solid != sample.solid || next.worldAxis != sample.worldAxis ||
          next.block != sample.block || next.targeted != sample.targeted ||
          next.displayColor != sample.displayColor) {
        break;
      }
      ++runEnd;
    }
    if (sample.solid) {
      const std::array<std::uint8_t, 3> color = sample.displayColor;
      SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], 255);
      const SDL_Rect rectangle{stripLeft, runStart, stripWidth,
                               runEnd - runStart};
      SDL_RenderFillRect(renderer, &rectangle);
    }
    runStart = runEnd;
  }

  drawFluidImmersion(renderer, world.blockAt(containingBlock(camera.position)),
                     width, height);

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderDrawLine(renderer, stripLeft, height / 2,
                     stripLeft + stripWidth - 1, height / 2);
  drawStatusBar(renderer, camera, width);
}

struct VisionGeometry3D {
  std::uint64_t startupCounter{SDL_GetPerformanceCounter()};
  bool initialStartupReported{};
  bool fullStartupReported{};
  BlockCoord3D center{};
  int radius{-1};
  std::uint64_t worldRevision{std::numeric_limits<std::uint64_t>::max()};
  bool geometryInitialized{};
  std::unordered_map<ChunkCoord, std::vector<VisibleFace3D>, ChunkCoordHash>
      chunkFaces;
  std::vector<VisibleFace3D> faces;
};

std::vector<ChunkCoord> chunksFor3D(const BlockCoord3D &center, int radius) {
  const ChunkCoord first = chunkCoordForBlock(
      {center.x - radius, center.y - radius, center.z - radius, 0});
  const ChunkCoord last = chunkCoordForBlock(
      {center.x + radius, center.y + radius, center.z + radius, 0});
  std::vector<ChunkCoord> chunks;
  for (int z = first.z; z <= last.z; ++z) {
    for (int y = first.y; y <= last.y; ++y) {
      for (int x = first.x; x <= last.x; ++x) {
        chunks.push_back({x, y, z, 0});
      }
    }
  }
  return chunks;
}

bool chunksTouch3D(const ChunkCoord &left, const ChunkCoord &right) {
  return left.w == right.w && std::abs(left.x - right.x) +
                                      std::abs(left.y - right.y) +
                                      std::abs(left.z - right.z) ==
                                  1;
}

ScreenPoint visionScreenPoint(const Vec2 &point, int width, int height) {
  return {static_cast<int>(std::lround((point.x + 1.0) * 0.5 * width)),
          static_cast<int>(std::lround((1.0 - point.y) * 0.5 * height))};
}

std::vector<VisibleFace3D> cloudFaces3D(const Vec3 &position) {
  std::vector<VisibleFace3D> faces;
  for (const Cloud3D &cloud : visibleClouds3D(position)) {
    for (int axis = 0; axis < 3; ++axis) {
      std::array<int, 2> tangentAxes{};
      int tangentIndex = 0;
      for (int candidate = 0; candidate < 3; ++candidate) {
        if (candidate != axis) {
          tangentAxes[static_cast<std::size_t>(tangentIndex++)] = candidate;
        }
      }
      for (const int direction : {-1, 1}) {
        VisibleFace3D face;
        face.worldAxis = axis;
        face.normalDirection = direction;
        face.material = BlockId::snow;
        constexpr std::array<std::array<int, 2>, 4> offsets{{
            {0, 0},
            {1, 0},
            {1, 1},
            {0, 1},
        }};
        for (std::size_t corner = 0; corner < face.corners.size(); ++corner) {
          Vec3 point = cloud.minimum;
          point[static_cast<std::size_t>(axis)] +=
              direction > 0 ? cloud.size : 0.0;
          point[static_cast<std::size_t>(tangentAxes[0])] +=
              offsets[corner][0] * cloud.size;
          point[static_cast<std::size_t>(tangentAxes[1])] +=
              offsets[corner][1] * cloud.size;
          face.corners[corner] = point;
        }
        faces.push_back(face);
      }
    }
  }
  return faces;
}

void drawClouds3D(SDL_Renderer *renderer, const Camera3D &camera, int width,
                  int height, double aspectRatio) {
  std::vector<ProjectedFace3D> projected =
      projectVisibleFaces3D(cloudFaces3D(camera.position), camera, aspectRatio);
  std::ranges::sort(projected, std::greater{}, &ProjectedFace3D::depth);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const ProjectedFace3D &face : projected) {
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.reserve(face.vertexCount);
    for (std::size_t corner = 0; corner < face.vertexCount; ++corner) {
      SDL_Vertex vertex{};
      vertex.position.x =
          static_cast<float>((face.vertices[corner].x + 1.0) * 0.5 * width);
      vertex.position.y =
          static_cast<float>((1.0 - face.vertices[corner].y) * 0.5 * height);
      vertex.color = SDL_Color{235, 242, 250, 72};
      vertices.push_back(vertex);
    }
    for (std::size_t corner = 1; corner + 1 < face.vertexCount; ++corner) {
      indices.push_back(0);
      indices.push_back(static_cast<int>(corner));
      indices.push_back(static_cast<int>(corner + 1));
    }
    if (!vertices.empty()) {
      static_cast<void>(SDL_RenderGeometry(
          renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
          indices.data(), static_cast<int>(indices.size())));
    }
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

bool render(SDL_Renderer *renderer, const BlockWorld3D &world,
            const Camera3D &camera, int width, int height,
            VisionGeometry3D &vision) {
  const BlockCoord3D center = containingBlock(camera.position);
  constexpr int initialRadius = 3;
  if (!vision.geometryInitialized || vision.center != center ||
      vision.worldRevision != world.revision() ||
      vision.radius < renderBlockRadius3D) {
    const int requestedRadius =
        vision.geometryInitialized ? renderBlockRadius3D : initialRadius;
    const std::vector<ChunkCoord> visibleChunks =
        chunksFor3D(center, requestedRadius);
    const bool ready = world.prepareChunks(visibleChunks);
    if (!vision.geometryInitialized) {
      static_cast<void>(
          world.prepareChunks(chunksFor3D(center, renderBlockRadius3D)));
    }
    if (ready) {
      if (vision.worldRevision != world.revision()) {
        vision.chunkFaces.clear();
      }
      std::vector<ChunkCoord> newlyVisible;
      for (const ChunkCoord chunk : visibleChunks) {
        if (!vision.chunkFaces.contains(chunk)) {
          newlyVisible.push_back(chunk);
        }
      }
      std::unordered_map<ChunkCoord, std::vector<VisibleFace3D>, ChunkCoordHash>
          retained;
      retained.reserve(visibleChunks.size());
      for (const ChunkCoord chunk : visibleChunks) {
        const bool touchesNewChunk =
            std::ranges::any_of(newlyVisible, [&](const ChunkCoord &added) {
              return chunksTouch3D(chunk, added);
            });
        if (auto existing = vision.chunkFaces.find(chunk);
            existing != vision.chunkFaces.end() && !touchesNewChunk) {
          retained.emplace(chunk, std::move(existing->second));
        } else {
          retained.emplace(chunk, buildVisibleFaces3DChunk(world, chunk));
        }
      }
      vision.chunkFaces = std::move(retained);
      vision.faces.clear();
      const BlockCoord3D minimum{center.x - requestedRadius,
                                 center.y - requestedRadius,
                                 center.z - requestedRadius};
      const BlockCoord3D maximum{center.x + requestedRadius,
                                 center.y + requestedRadius,
                                 center.z + requestedRadius};
      for (const auto &[chunk, faces] : vision.chunkFaces) {
        static_cast<void>(chunk);
        for (const VisibleFace3D &face : faces) {
          if (face.block.x >= minimum.x && face.block.x <= maximum.x &&
              face.block.y >= minimum.y && face.block.y <= maximum.y &&
              face.block.z >= minimum.z && face.block.z <= maximum.z) {
            vision.faces.push_back(face);
          }
        }
      }
      vision.center = center;
      vision.radius = requestedRadius;
      vision.worldRevision = world.revision();
      vision.geometryInitialized = true;
      reportStartupStage("3D", requestedRadius, renderBlockRadius3D,
                         world.loadedChunkCount(), vision.startupCounter,
                         vision.initialStartupReported,
                         vision.fullStartupReported);
    }
  }
  SDL_SetRenderDrawColor(renderer, skyColor.red, skyColor.green, skyColor.blue,
                         255);
  SDL_RenderClear(renderer);
  const double aspectRatio =
      static_cast<double>(width) / static_cast<double>(height);
  drawClouds3D(renderer, camera, width, height, aspectRatio);
  std::vector<ProjectedFace3D> projected =
      projectVisibleFaces3D(vision.faces, camera, aspectRatio);
  std::ranges::sort(projected, std::greater{}, &ProjectedFace3D::depth);
  std::vector<SDL_Vertex> vertices;
  std::vector<int> indices;
  vertices.reserve(projected.size() * 4U);
  indices.reserve(projected.size() * 6U);
  for (const ProjectedFace3D &face : projected) {
    const int firstVertex = static_cast<int>(vertices.size());
    const auto color = visionBlockColor3D(face.material, face.block);
    for (std::size_t corner = 0; corner < face.vertexCount; ++corner) {
      const Vec2 &point = face.vertices[corner];
      SDL_Vertex vertex{};
      vertex.position.x = static_cast<float>((point.x + 1.0) * 0.5 * width);
      vertex.position.y = static_cast<float>((1.0 - point.y) * 0.5 * height);
      vertex.color =
          SDL_Color{color[0], color[1], color[2], blockOpacity(face.material)};
      vertices.push_back(vertex);
    }
    for (std::size_t corner = 1; corner + 1 < face.vertexCount; ++corner) {
      indices.push_back(firstVertex);
      indices.push_back(firstVertex + static_cast<int>(corner));
      indices.push_back(firstVertex + static_cast<int>(corner + 1));
    }
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const int geometryResult =
      vertices.empty() ? 0
                       : SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                                            static_cast<int>(vertices.size()),
                                            indices.data(),
                                            static_cast<int>(indices.size()));
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  if (geometryResult != 0) {
    return false;
  }

  drawFluidImmersion(renderer, world.blockAt(containingBlock(camera.position)),
                     width, height);

  if (const auto hit = raycast(world, camera.position, camera.forward(), 8.0)) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (const Line2D &line :
         buildCubeSelectionWireframe3D(hit->block, camera, aspectRatio)) {
      const ScreenPoint from = visionScreenPoint(line.from, width, height);
      const ScreenPoint to = visionScreenPoint(line.to, width, height);
      SDL_RenderDrawLine(renderer, from.x, from.y, to.x, to.y);
    }
  }

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  constexpr int crosshairRadius = 6;
  SDL_RenderDrawLine(renderer, width / 2 - crosshairRadius, height / 2,
                     width / 2 + crosshairRadius, height / 2);
  SDL_RenderDrawLine(renderer, width / 2, height / 2 - crosshairRadius,
                     width / 2, height / 2 + crosshairRadius);
  drawStatusBar(renderer, camera, width);
  return true;
}

bool saveRendererBitmap(SDL_Renderer *renderer, int width, int height,
                        const std::string &path) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (surface == nullptr) {
    return false;
  }
  const int result =
      SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                           surface->pixels, surface->pitch);
  const bool saved = result == 0 && SDL_SaveBMP(surface, path.c_str()) == 0;
  SDL_FreeSurface(surface);
  return saved;
}

int runThreeDimensionalSession(SDL_Renderer *renderer, SDL_Window *window,
                               TerrainMode terrainMode, bool smokeTest,
                               bool inventorySmoke, bool fluidSmoke,
                               const std::string &smokeOutput) {
  const std::string worldName = terrainModeName(terrainMode);
  SDL_SetWindowTitle(window, ("Proj4D | 3D " + worldName +
                              " | Mouse: look | WASD: move | Shift: sneak")
                                 .c_str());
  GameSession game(terrainMode, PlayDimension::Three);
  BlockWorld &sharedWorld = game.world();
  std::optional<PersistentWorldSession> persistence;
  if (!smokeTest) {
    persistence.emplace();
    std::string saveError;
    if (!initializePersistentWorld(game, *persistence, saveError)) {
      std::cerr << "Could not open the selected world: " << saveError << '\n';
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "World save error",
                               saveError.c_str(), window);
      return 1;
    }
  }
  BlockWorld3D world(sharedWorld);
  Camera3D camera;
  const int spawnSurface = world.surfaceHeightAt(0, 0);
  const Vec3 spawnPosition{
      0.5,
      static_cast<double>(spawnSurface + 1) + playerEyeHeight,
      0.5,
  };
  camera.position = spawnPosition;
  camera.turnVertical(-0.28);
  if (fluidSmoke) {
    for (int x = 2; x <= 5; ++x) {
      for (int z = -2; z <= 2; ++z) {
        static_cast<void>(
            world.setBlock({x, 1, z}, z <= 0 ? BlockId::water : BlockId::lava));
      }
    }
  }
  PlayerMotionState3D playerMotion{0.0, true, spawnPosition};
  VisionGeometry3D vision;
  InventoryUiState inventoryUi;
  if (inventorySmoke) {
    inventoryUi.open = true;
    InventoryState preview;
    preview.storage[0] = normalizedStack({BlockId::grass, 64});
    preview.storage[1] = normalizedStack({BlockId::wood, 32});
    preview.storage[2] = normalizedStack({BlockId::stone, 128});
    preview.quick[0] = normalizedStack(
        {BlockId::iron_tool, 1, maximumDurability(BlockId::iron_tool)});
    preview.quick[1] = normalizedStack({BlockId::planks, 24});
    preview.handCrafting[0] = normalizedStack({BlockId::wood, 1});
    game.replaceInventory(std::move(preview));
    pointAtInventorySmokeItem(window, renderer, game.inventory(),
                              PlayDimension::Three, inventoryUi);
  }
  bool running = true;
  bool miningHeld = false;
  bool renderFailed = false;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D 3D " << worldName << " world (shared w=0 slice)\n"
              << "Controls:\n"
              << "  W/S: move forward/backward\n"
              << "  A/D: move left/right\n"
              << "  Mouse: look around\n"
              << "  Space: jump 1.5 blocks or swim upward\n"
              << "  Hold Shift: sneak on land or swim downward\n"
              << "  Left click: break a targeted cube\n"
              << "  Right click: build a cube\n"
              << "  Escape: quit\n";
  }

  int frameCount = 0;
  while (running) {
    const std::uint64_t counter = SDL_GetPerformanceCounter();
    const double elapsed = static_cast<double>(counter - previousCounter) /
                           static_cast<double>(SDL_GetPerformanceFrequency());
    previousCounter = counter;
    const double deltaSeconds =
        smokeTest ? 1.0 / 60.0 : std::min(elapsed, 0.05);

    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE) {
        if (inventoryUi.open) {
          inventoryUi.open = false;
          SDL_SetRelativeMouseMode(smokeTest ? SDL_FALSE : SDL_TRUE);
        } else {
          running = false;
        }
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_t &&
                 event.key.repeat == 0) {
        inventoryUi.open = !inventoryUi.open;
        inventoryUi.station = CraftingStation::Hand;
        SDL_SetRelativeMouseMode(inventoryUi.open || smokeTest ? SDL_FALSE
                                                               : SDL_TRUE);
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym >= SDLK_1 &&
                 event.key.keysym.sym <= SDLK_9) {
        InventoryState changed = game.inventory();
        selectQuickSlot(changed, PlayDimension::Three,
                        static_cast<int>(event.key.keysym.sym - SDLK_1));
        game.replaceInventory(std::move(changed));
      } else if (event.type == SDL_MOUSEMOTION && !smokeTest &&
                 !inventoryUi.open) {
        camera.turnHorizontal(static_cast<double>(event.motion.xrel) *
                              mouseLookRadiansPerPixel);
        camera.turnVertical(-static_cast<double>(event.motion.yrel) *
                            mouseLookRadiansPerPixel);
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (inventoryUi.open) {
          int width = 1;
          int height = 1;
          SDL_GetRendererOutputSize(renderer, &width, &height);
          if (applyInventoryUiClick(game, PlayDimension::Three, inventoryUi,
                                    width, height, event.button.x,
                                    event.button.y, event.button.button) &&
              persistence) {
            static_cast<void>(persistChangedWorld(game, *persistence));
          }
          continue;
        }
        bool worldChanged = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
          miningHeld = true;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord3D, 2> protectedBlocks{{
              containingBlock(camera.position),
              playerLowerBodyBlock(camera.position,
                                   playerMotion.sneaking
                                       ? playerSneakCollisionBounds
                                       : playerCollisionBounds),
          }};
          if (const auto hit =
                  raycast(world, camera.position, camera.forward(), 8.0)) {
            if (world.blockAt(hit->block) == BlockId::crafting_table) {
              inventoryUi.open = true;
              inventoryUi.station = CraftingStation::Table;
              SDL_SetRelativeMouseMode(SDL_FALSE);
              continue;
            }
            const std::array<BlockCoord, 2> protected4D{{
                blockCoord4D(protectedBlocks[0]),
                blockCoord4D(protectedBlocks[1]),
            }};
            worldChanged =
                game.placeSelected(blockCoord4D(hit->placement), protected4D);
          }
        }
        if (worldChanged && persistence) {
          static_cast<void>(persistChangedWorld(game, *persistence));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP &&
                 event.button.button == SDL_BUTTON_LEFT) {
        miningHeld = false;
        game.cancelMining();
      }
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const PlayerMoveInput3D movement{
        (keys[SDL_SCANCODE_W] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_S] != 0 ? 1.0 : 0.0),
        (keys[SDL_SCANCODE_D] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_A] != 0 ? 1.0 : 0.0),
        keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0,
    };
    if (!inventoryUi.open) {
      updatePlayerMotion(camera, world, playerMotion, movement,
                         keys[SDL_SCANCODE_SPACE] != 0, deltaSeconds);
    }
    const auto miningHit =
        raycast(world, camera.position, camera.forward(), 8.0);
    if (game.updateMining(miningHit
                              ? std::optional(blockCoord4D(miningHit->block))
                              : std::nullopt,
                          miningHeld && !inventoryUi.open, deltaSeconds) &&
        persistence) {
      static_cast<void>(persistChangedWorld(game, *persistence));
    }

    int width = 1;
    int height = 1;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    if (!render(renderer, world, camera, width, height, vision)) {
      std::cerr << "Could not render the 3D view: " << SDL_GetError() << '\n';
      renderFailed = true;
      running = false;
      continue;
    }
    drawQuickStorage(renderer, game.inventory(), PlayDimension::Three, width,
                     height);
    if (inventoryUi.open) {
      drawInventoryOverlay(renderer, game.inventory(), PlayDimension::Three,
                           inventoryUi, width, height);
    }
    SDL_RenderPresent(renderer);

    ++frameCount;
    if (!smokeTest && frameCount % 60 == 0) {
      const std::string title = "Proj4D | 3D " + worldName +
                                " | shared w=0 slice | loaded " +
                                std::to_string(world.loadedChunkCount()) +
                                " | WASD move | Mouse look | Shift sneak";
      SDL_SetWindowTitle(window, title.c_str());
    }
    if (smokeTest &&
        (!vision.geometryInitialized || vision.radius < renderBlockRadius3D)) {
      SDL_Delay(1U);
    }
    if (smokeTest && frameCount >= 3 && vision.geometryInitialized &&
        vision.radius == renderBlockRadius3D) {
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save 3D smoke image: " << SDL_GetError()
                  << '\n';
        renderFailed = true;
      }
      running = false;
    }
  }

  const bool saved = !persistence || persistChangedWorld(game, *persistence);
  return renderFailed || !saved ? 1 : 0;
}

int runTwoDimensionalSession(SDL_Renderer *renderer, SDL_Window *window,
                             TerrainMode terrainMode, bool smokeTest,
                             bool inventorySmoke, bool fluidSmoke,
                             const std::string &smokeOutput) {
  const std::string worldName = terrainModeName(terrainMode);
  SDL_SetWindowTitle(window,
                     ("Proj4D | 2D " + worldName +
                      " | Mouse: vertical look | Z: reverse | Shift: sneak")
                         .c_str());
  GameSession game(terrainMode, PlayDimension::Two);
  BlockWorld &sharedWorld = game.world();
  std::optional<PersistentWorldSession> persistence;
  if (!smokeTest) {
    persistence.emplace();
    std::string saveError;
    if (!initializePersistentWorld(game, *persistence, saveError)) {
      std::cerr << "Could not open the selected world: " << saveError << '\n';
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "World save error",
                               saveError.c_str(), window);
      return 1;
    }
  }
  BlockWorld2D world(sharedWorld);
  Camera2D camera;
  const int spawnSurface = world.surfaceHeightAt(0);
  const Vec2 spawnPosition{
      0.5,
      static_cast<double>(spawnSurface + 1) + playerEyeHeight,
  };
  camera.position = spawnPosition;
  camera.turnVertical(-0.28);
  if (fluidSmoke) {
    for (int x = 2; x <= 5; ++x) {
      static_cast<void>(
          world.setBlock({x, 1}, x <= 3 ? BlockId::water : BlockId::lava));
    }
  }
  PlayerMotionState2D playerMotion{0.0, true, spawnPosition};
  VisionGeometry2D vision;
  InventoryUiState inventoryUi;
  if (inventorySmoke) {
    inventoryUi.open = true;
    InventoryState preview;
    const auto storage = visibleStorageSlots(PlayDimension::Two);
    preview.storage[storage[0]] = normalizedStack({BlockId::grass, 64});
    preview.storage[storage[1]] = normalizedStack({BlockId::wood, 32});
    const auto quick = visibleQuickSlots(PlayDimension::Two);
    preview.quick[quick[0]] = normalizedStack(
        {BlockId::stone_tool, 1, maximumDurability(BlockId::stone_tool)});
    preview.handCrafting[0] = normalizedStack({BlockId::wood, 1});
    game.replaceInventory(std::move(preview));
    pointAtInventorySmokeItem(window, renderer, game.inventory(),
                              PlayDimension::Two, inventoryUi);
  }
  bool running = true;
  bool miningHeld = false;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D 2D " << worldName << " world\n"
              << "Controls:\n"
              << "  W/S: move forward/backward in 2D\n"
              << "  Mouse up/down: look up/down\n"
              << "  Z: reverse the horizontal viewing direction\n"
              << "  Space: jump 1.5 blocks or swim upward\n"
              << "  Hold Shift: sneak on land or swim downward\n"
              << "  Left click: break a targeted square\n"
              << "  Right click: build a square\n"
              << "  Escape: quit\n";
  }

  int frameCount = 0;
  while (running) {
    const std::uint64_t counter = SDL_GetPerformanceCounter();
    const double elapsed = static_cast<double>(counter - previousCounter) /
                           static_cast<double>(SDL_GetPerformanceFrequency());
    previousCounter = counter;
    const double deltaSeconds =
        smokeTest ? 1.0 / 60.0 : std::min(elapsed, 0.05);

    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE) {
        if (inventoryUi.open) {
          inventoryUi.open = false;
          SDL_SetRelativeMouseMode(smokeTest ? SDL_FALSE : SDL_TRUE);
        } else {
          running = false;
        }
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_t &&
                 event.key.repeat == 0) {
        inventoryUi.open = !inventoryUi.open;
        inventoryUi.station = CraftingStation::Hand;
        SDL_SetRelativeMouseMode(inventoryUi.open || smokeTest ? SDL_FALSE
                                                               : SDL_TRUE);
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym >= SDLK_1 &&
                 event.key.keysym.sym <= SDLK_3) {
        InventoryState changed = game.inventory();
        selectQuickSlot(changed, PlayDimension::Two,
                        static_cast<int>(event.key.keysym.sym - SDLK_1));
        game.replaceInventory(std::move(changed));
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_z &&
                 event.key.repeat == 0 && !inventoryUi.open) {
        camera.reverseHorizontalView();
      } else if (event.type == SDL_MOUSEMOTION && !smokeTest &&
                 !inventoryUi.open) {
        camera.turnVertical(-static_cast<double>(event.motion.yrel) *
                            mouseLookRadiansPerPixel);
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (inventoryUi.open) {
          int width = 1;
          int height = 1;
          SDL_GetRendererOutputSize(renderer, &width, &height);
          if (applyInventoryUiClick(game, PlayDimension::Two, inventoryUi,
                                    width, height, event.button.x,
                                    event.button.y, event.button.button) &&
              persistence) {
            static_cast<void>(persistChangedWorld(game, *persistence));
          }
          continue;
        }
        bool worldChanged = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
          miningHeld = true;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord2D, 2> protectedBlocks{{
              containingBlock(camera.position),
              playerLowerBodyBlock(camera.position,
                                   playerMotion.sneaking
                                       ? playerSneakCollisionBounds
                                       : playerCollisionBounds),
          }};
          if (const auto hit =
                  raycast(world, camera.position, camera.forward(), 8.0)) {
            if (world.blockAt(hit->block) == BlockId::crafting_table) {
              inventoryUi.open = true;
              inventoryUi.station = CraftingStation::Table;
              SDL_SetRelativeMouseMode(SDL_FALSE);
              continue;
            }
            const std::array<BlockCoord, 2> protected4D{{
                blockCoord4D(protectedBlocks[0]),
                blockCoord4D(protectedBlocks[1]),
            }};
            worldChanged =
                game.placeSelected(blockCoord4D(hit->placement), protected4D);
          }
        }
        if (worldChanged && persistence) {
          static_cast<void>(persistChangedWorld(game, *persistence));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP &&
                 event.button.button == SDL_BUTTON_LEFT) {
        miningHeld = false;
        game.cancelMining();
      }
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const PlayerMoveInput2D movement{
        (keys[SDL_SCANCODE_W] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_S] != 0 ? 1.0 : 0.0),
        keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0,
    };
    if (!inventoryUi.open) {
      updatePlayerMotion(camera, world, playerMotion, movement,
                         keys[SDL_SCANCODE_SPACE] != 0, deltaSeconds);
    }
    const auto miningHit =
        raycast(world, camera.position, camera.forward(), 8.0);
    if (game.updateMining(miningHit
                              ? std::optional(blockCoord4D(miningHit->block))
                              : std::nullopt,
                          miningHeld && !inventoryUi.open, deltaSeconds) &&
        persistence) {
      static_cast<void>(persistChangedWorld(game, *persistence));
    }

    int width = 1;
    int height = 1;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    render(renderer, world, camera, width, height, vision);
    drawQuickStorage(renderer, game.inventory(), PlayDimension::Two, width,
                     height);
    if (inventoryUi.open) {
      drawInventoryOverlay(renderer, game.inventory(), PlayDimension::Two,
                           inventoryUi, width, height);
    }
    SDL_RenderPresent(renderer);

    ++frameCount;
    if (!smokeTest && frameCount % 60 == 0) {
      const std::string title =
          "Proj4D | 2D " + worldName + " | shared 16^4 chunks | loaded " +
          std::to_string(world.loadedChunkCount()) +
          " | W/S move | Mouse: vertical | Z: reverse | Shift: sneak";
      SDL_SetWindowTitle(window, title.c_str());
    }
    if (smokeTest &&
        (!vision.initialized || vision.radius < terrainRenderDistance2D)) {
      SDL_Delay(1U);
    }
    if (smokeTest && frameCount >= 3 && vision.initialized &&
        vision.radius == terrainRenderDistance2D) {
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save 2D smoke image: " << SDL_GetError()
                  << '\n';
        frameCount = -1;
      }
      running = false;
    }
  }
  const bool saved = !persistence || persistChangedWorld(game, *persistence);
  return frameCount < 0 || !saved ? 1 : 0;
}

} // namespace

int runApplication(RunMode mode, const std::string &smokeOutput) {
  const bool smokeTest = mode != RunMode::Interactive;
  if (smokeTest) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

  constexpr int initialWidth = 1120;
  constexpr int initialHeight = 800;
  const Uint32 windowFlags =
      SDL_WINDOW_RESIZABLE | (smokeTest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
  SDL_Window *window = SDL_CreateWindow(
      "Proj4D | Select World", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      initialWidth, initialHeight, windowFlags);
  if (window == nullptr) {
    std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1,
      smokeTest ? SDL_RENDERER_SOFTWARE
                : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == nullptr && !smokeTest) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (renderer == nullptr) {
    std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (mode == RunMode::MenuSmokeTest) {
    int width = initialWidth;
    int height = initialHeight;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    drawDimensionMenu(renderer, WorldDimension::Four, width, height);
    const bool saved = saveRendererBitmap(renderer, width, height, smokeOutput);
    if (!saved) {
      std::cerr << "Could not save menu smoke image: " << SDL_GetError()
                << '\n';
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return saved ? 0 : 1;
  }

  const bool twoDimensionalSmoke = mode == RunMode::TwoDTerrainMenuSmokeTest ||
                                   mode == RunMode::TwoDFlatSmokeTest ||
                                   mode == RunMode::TwoDLowSmokeTest ||
                                   mode == RunMode::TwoDHighSmokeTest ||
                                   mode == RunMode::TwoDInventorySmokeTest ||
                                   mode == RunMode::TwoDFluidSmokeTest;
  const bool threeDimensionalSmoke =
      mode == RunMode::ThreeDTerrainMenuSmokeTest ||
      mode == RunMode::ThreeDFlatSmokeTest ||
      mode == RunMode::ThreeDLowSmokeTest ||
      mode == RunMode::ThreeDHighSmokeTest ||
      mode == RunMode::ThreeDInventorySmokeTest ||
      mode == RunMode::ThreeDFluidSmokeTest;
  std::optional<WorldDimension> selectedDimension;
  if (mode == RunMode::Interactive) {
    selectedDimension = chooseWorldDimension(renderer);
  } else {
    selectedDimension = twoDimensionalSmoke
                            ? WorldDimension::Two
                            : (threeDimensionalSmoke ? WorldDimension::Three
                                                     : WorldDimension::Four);
  }
  if (!selectedDimension) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  }

  if (mode == RunMode::FourDTerrainMenuSmokeTest ||
      mode == RunMode::ThreeDTerrainMenuSmokeTest ||
      mode == RunMode::TwoDTerrainMenuSmokeTest) {
    int width = initialWidth;
    int height = initialHeight;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    drawTerrainMenu(renderer, *selectedDimension, TerrainMode::Flat, width,
                    height);
    const bool saved = saveRendererBitmap(renderer, width, height, smokeOutput);
    if (!saved) {
      std::cerr << "Could not save terrain menu smoke image: " << SDL_GetError()
                << '\n';
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return saved ? 0 : 1;
  }

  std::optional<TerrainMode> selectedTerrain;
  if (mode == RunMode::Interactive) {
    selectedTerrain = chooseTerrainMode(renderer, *selectedDimension);
  } else if (mode == RunMode::LowSmokeTest ||
             mode == RunMode::ThreeDLowSmokeTest ||
             mode == RunMode::TwoDLowSmokeTest) {
    selectedTerrain = TerrainMode::Low;
  } else if (mode == RunMode::HighSmokeTest ||
             mode == RunMode::ThreeDHighSmokeTest ||
             mode == RunMode::TwoDHighSmokeTest) {
    selectedTerrain = TerrainMode::Density;
  } else {
    selectedTerrain = TerrainMode::Flat;
  }
  if (!selectedTerrain) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  }

  if (*selectedDimension == WorldDimension::Two) {
    const int result = runTwoDimensionalSession(
        renderer, window, *selectedTerrain, smokeTest,
        mode == RunMode::TwoDInventorySmokeTest,
        mode == RunMode::TwoDFluidSmokeTest, smokeOutput);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
  }

  if (*selectedDimension == WorldDimension::Three) {
    const int result = runThreeDimensionalSession(
        renderer, window, *selectedTerrain, smokeTest,
        mode == RunMode::ThreeDInventorySmokeTest,
        mode == RunMode::ThreeDFluidSmokeTest, smokeOutput);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
  }

  const std::string worldName = terrainModeName(*selectedTerrain);
  SDL_SetWindowTitle(window, ("Proj4D | " + worldName +
                              " World | Mouse: 4D look | Tab: vertical | "
                              "Ctrl: orbit | M: slice | Shift: sneak | "
                              "WASDQE move")
                                 .c_str());
  GameSession game(*selectedTerrain, PlayDimension::Four);
  BlockWorld &world = game.world();
  std::optional<PersistentWorldSession> persistence;
  if (!smokeTest) {
    persistence.emplace();
    std::string saveError;
    if (!initializePersistentWorld(game, *persistence, saveError)) {
      std::cerr << "Could not open the selected world: " << saveError << '\n';
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "World save error",
                               saveError.c_str(), window);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
  }
  Camera4D camera;
  const int spawnSurface = world.surfaceHeightAt(0, 0, 0);
  const Vec4 spawnPosition{
      0.5,
      static_cast<double>(spawnSurface + 1) + playerEyeHeight,
      0.5,
      0.5,
  };
  camera.position = spawnPosition;
  camera.turnVertical(-0.28);
  if (mode == RunMode::FourDFluidSmokeTest) {
    for (int z = 2; z <= 5; ++z) {
      for (int x = -2; x <= 2; ++x) {
        for (int w = -2; w <= 2; ++w) {
          static_cast<void>(world.setBlock(
              {x, 1, z, w}, x <= 0 ? BlockId::water : BlockId::lava));
        }
      }
    }
  }
  DisplayCamera display;
  FeatureGeometryCache geometryCache;
  InventoryUiState inventoryUi;
  InventoryRightGesture inventoryRightGesture;
  FourDQuickSelectionState quickSelection;
  VolumeViewMode volumeViewMode = VolumeViewMode::Solid;
  if (mode == RunMode::FourDInventorySmokeTest) {
    inventoryUi.open = true;
    InventoryState preview;
    preview.storage[0] = normalizedStack({BlockId::grass, 64});
    preview.storage[5] = normalizedStack({BlockId::wood, 32});
    preview.storage[18] = normalizedStack({BlockId::copper_ore, 16});
    preview.storage[42] = normalizedStack({BlockId::glass, 24});
    preview.quick[0] = normalizedStack(
        {BlockId::iron_tool, 1, maximumDurability(BlockId::iron_tool)});
    preview.quick[13] = normalizedStack({BlockId::planks, 24});
    preview.handCrafting[0] = normalizedStack({BlockId::wood, 1});
    game.replaceInventory(std::move(preview));
    pointAtInventorySmokeItem(window, renderer, game.inventory(),
                              PlayDimension::Four, inventoryUi);
  }
  PlayerMotionState playerMotion{0.0, true, spawnPosition};
  bool running = true;
  bool miningHeld = false;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (mode == RunMode::FourDSliceSmokeTest) {
    SDL_Event toggleEvent{};
    toggleEvent.type = SDL_KEYDOWN;
    toggleEvent.key.keysym.sym = SDLK_m;
    toggleEvent.key.repeat = 0;
    if (SDL_PushEvent(&toggleEvent) < 0) {
      std::cerr << "Could not queue 4D slice toggle: " << SDL_GetError()
                << '\n';
      SDL_DestroyTexture(geometryCache.volumeTexture);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
  }

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D " << worldName << " world\n"
              << "Controls:\n"
              << "  W/S: move forward/backward in 4D\n"
              << "  A/D: move along the first sideways direction\n"
              << "  Q/E: move along the second sideways direction\n"
              << "  Space: jump 1.5 blocks or swim upward\n"
              << "  Hold Shift: sneak on land or swim downward\n"
              << "  Up/Down: look up/down (limited to straight up/down)\n"
              << "  Mouse left/right: ordinary horizontal look\n"
              << "  Mouse up/down: fourth-dimensional look\n"
              << "  Hold Tab + mouse up/down: vertical look\n"
              << "  Hold Ctrl + mouse: orbit the solid 3D vision cube\n"
              << "  Mouse wheel: zoom the vision cube\n"
              << "  M: toggle the solid projection or centered square slice\n"
              << "  1-3 then 1-9 rapidly: select a 4D hotbar layer and slot\n"
              << "  Left click: break a targeted tesseract\n"
              << "  Right click: build a tesseract\n"
              << "  Inventory right click: split/place one item\n"
              << "  Inventory right drag: rotate the inventory cube\n"
              << "  Escape: quit\n";
  }

  int frameCount = 0;
  while (running) {
    const std::uint64_t counter = SDL_GetPerformanceCounter();
    const double elapsed = static_cast<double>(counter - previousCounter) /
                           static_cast<double>(SDL_GetPerformanceFrequency());
    previousCounter = counter;
    const double deltaSeconds =
        smokeTest ? 1.0 / 60.0 : std::min(elapsed, 0.05);

    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE) {
        if (inventoryUi.open) {
          inventoryUi.open = false;
          cancelInventoryRightGesture(inventoryRightGesture);
          SDL_SetRelativeMouseMode(smokeTest ? SDL_FALSE : SDL_TRUE);
        } else {
          running = false;
        }
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_t &&
                 event.key.repeat == 0) {
        inventoryUi.open = !inventoryUi.open;
        inventoryUi.station = CraftingStation::Hand;
        cancelInventoryRightGesture(inventoryRightGesture);
        SDL_SetRelativeMouseMode(inventoryUi.open || smokeTest ? SDL_FALSE
                                                               : SDL_TRUE);
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_m &&
                 event.key.repeat == 0 && !inventoryUi.open) {
        volumeViewMode = toggledVolumeViewMode(volumeViewMode);
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym >= SDLK_1 &&
                 event.key.keysym.sym <= SDLK_9 && event.key.repeat == 0) {
        InventoryState changed = game.inventory();
        if (enterFourDQuickDigit(
                changed, quickSelection,
                static_cast<int>(event.key.keysym.sym - SDLK_0),
                static_cast<std::uint64_t>(event.key.timestamp))) {
          game.replaceInventory(std::move(changed));
        }
      } else if (event.type == SDL_MOUSEMOTION && !smokeTest) {
        if (inventoryUi.open) {
          if ((event.motion.state & SDL_BUTTON_RMASK) != 0U &&
              updateInventoryRightGesture(inventoryRightGesture, event.motion.x,
                                          event.motion.y)) {
            inventoryUi.yaw += static_cast<double>(event.motion.xrel) * 0.008;
            inventoryUi.pitch =
                std::clamp(inventoryUi.pitch +
                               static_cast<double>(event.motion.yrel) * 0.008,
                           -pi * 0.48, pi * 0.48);
          }
          continue;
        }
        const SDL_Keymod modifiers = SDL_GetModState();
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const MouseMotionMode mouseMode = selectMouseMotionMode(
            (modifiers & KMOD_CTRL) != 0, keys[SDL_SCANCODE_TAB] != 0);
        const MouseMotionMapping mouseMotion =
            mapMouseMotion(static_cast<double>(event.motion.xrel),
                           static_cast<double>(event.motion.yrel), mouseMode);
        camera.turnHorizontal(mouseMotion.worldHorizontalTurn);
        camera.turnVertical(mouseMotion.worldVerticalTurn);
        camera.turnFourth(mouseMotion.worldFourthTurn);
        display.yaw += mouseMotion.visionCubeYawTurn;
        display.pitch =
            std::clamp(display.pitch + mouseMotion.visionCubePitchTurn,
                       -pi * 0.48, pi * 0.48);
      } else if (event.type == SDL_MOUSEWHEEL) {
        display.distance = std::clamp(
            display.distance - static_cast<double>(event.wheel.y) * 0.2, 2.6,
            6.5);
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (inventoryUi.open) {
          int width = 1;
          int height = 1;
          SDL_GetRendererOutputSize(renderer, &width, &height);
          if (event.button.button == SDL_BUTTON_RIGHT) {
            beginInventoryRightGesture(inventoryRightGesture, event.button.x,
                                       event.button.y);
          } else {
            if (applyInventoryUiClick(game, PlayDimension::Four, inventoryUi,
                                      width, height, event.button.x,
                                      event.button.y, event.button.button) &&
                persistence) {
              static_cast<void>(persistChangedWorld(game, *persistence));
            }
          }
          continue;
        }
        bool worldChanged = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
          miningHeld = true;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord, 2> protectedBlocks{{
              containingBlock(camera.position),
              playerLowerBodyBlock(camera.position,
                                   playerMotion.sneaking
                                       ? playerSneakCollisionBounds
                                       : playerCollisionBounds),
          }};
          if (const auto hit =
                  raycast(world, camera.position, camera.forward, 8.0)) {
            if (world.blockAt(hit->block) == BlockId::crafting_table) {
              inventoryUi.open = true;
              inventoryUi.station = CraftingStation::Table;
              cancelInventoryRightGesture(inventoryRightGesture);
              SDL_SetRelativeMouseMode(SDL_FALSE);
              continue;
            }
            worldChanged = game.placeSelected(hit->placement, protectedBlocks);
          }
        }
        if (worldChanged && persistence) {
          static_cast<void>(persistChangedWorld(game, *persistence));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && inventoryUi.open &&
                 event.button.button == SDL_BUTTON_RIGHT) {
        const int clickX = inventoryRightGesture.pressX;
        const int clickY = inventoryRightGesture.pressY;
        if (finishInventoryRightGesture(inventoryRightGesture)) {
          int width = 1;
          int height = 1;
          SDL_GetRendererOutputSize(renderer, &width, &height);
          if (applyInventoryUiClick(game, PlayDimension::Four, inventoryUi,
                                    width, height, clickX, clickY,
                                    SDL_BUTTON_RIGHT) &&
              persistence) {
            static_cast<void>(persistChangedWorld(game, *persistence));
          }
        }
      } else if (event.type == SDL_MOUSEBUTTONUP &&
                 event.button.button == SDL_BUTTON_LEFT) {
        miningHeld = false;
        game.cancelMining();
      }
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const PlayerMoveInput movement{
        (keys[SDL_SCANCODE_W] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_S] != 0 ? 1.0 : 0.0),
        (keys[SDL_SCANCODE_D] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_A] != 0 ? 1.0 : 0.0),
        (keys[SDL_SCANCODE_E] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_Q] != 0 ? 1.0 : 0.0),
        keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0,
    };
    constexpr double turnSpeed = 1.25;
    camera.turnVertical(((keys[SDL_SCANCODE_UP] != 0 ? 1.0 : 0.0) -
                         (keys[SDL_SCANCODE_DOWN] != 0 ? 1.0 : 0.0)) *
                        turnSpeed * deltaSeconds);
    if (!inventoryUi.open) {
      updatePlayerMotion(camera, world, playerMotion, movement,
                         keys[SDL_SCANCODE_SPACE] != 0, deltaSeconds);
    }
    const auto miningHit = raycast(world, camera.position, camera.forward, 8.0);
    if (game.updateMining(miningHit ? std::optional(miningHit->block)
                                    : std::nullopt,
                          miningHeld && !inventoryUi.open, deltaSeconds) &&
        persistence) {
      static_cast<void>(persistChangedWorld(game, *persistence));
    }

    int width = initialWidth;
    int height = initialHeight;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    render(renderer, world, camera, display, width, height, volumeViewMode,
           geometryCache);
    const std::uint64_t nowMilliseconds = SDL_GetTicks();
    const bool awaitingQuickSlot =
        quickSelection.pendingLayer >= 0 &&
        nowMilliseconds <= quickSelection.deadlineMilliseconds;
    drawQuickStorage(renderer, game.inventory(), PlayDimension::Four, width,
                     height,
                     activeFourDQuickLayer(game.inventory(), quickSelection,
                                           nowMilliseconds),
                     awaitingQuickSlot);
    if (inventoryUi.open) {
      drawInventoryOverlay(renderer, game.inventory(), PlayDimension::Four,
                           inventoryUi, width, height);
    }
    SDL_RenderPresent(renderer);

    ++frameCount;
    if (!smokeTest && frameCount % 60 == 0) {
      const std::string title = "Proj4D | " + worldName +
                                " | infinite 16^4 chunks | loaded " +
                                std::to_string(world.loadedChunkCount()) +
                                " | WASDQE move | Mouse: 4D look | "
                                "Tab: vertical | Shift: sneak | "
                                "Ctrl: orbit | M: slice";
      SDL_SetWindowTitle(window, title.c_str());
    }
    if (smokeTest && (!geometryCache.initialized ||
                      geometryCache.radius < renderBlockRadius)) {
      SDL_Delay(1U);
    }
    if (smokeTest && frameCount >= 3 && geometryCache.initialized &&
        geometryCache.radius == renderBlockRadius) {
      if (mode == RunMode::FourDSliceSmokeTest &&
          volumeViewMode != VolumeViewMode::CenterSlice) {
        std::cerr << "4D slice smoke toggle did not activate\n";
        frameCount = -1;
        running = false;
        continue;
      }
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save smoke image: " << SDL_GetError() << '\n';
        frameCount = -1;
      }
      running = false;
    }
  }

  const bool saved = !persistence || persistChangedWorld(game, *persistence);

  SDL_DestroyTexture(geometryCache.volumeTexture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return frameCount < 0 || !saved ? 1 : 0;
}

} // namespace proj4d
