#include "proj4d/sdl_view.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/camera2d.hpp"
#include "proj4d/camera3d.hpp"
#include "proj4d/mouse_input.hpp"
#include "proj4d/player_motion.hpp"
#include "proj4d/player_motion2d.hpp"
#include "proj4d/player_motion3d.hpp"
#include "proj4d/render2d.hpp"
#include "proj4d/render3d.hpp"
#include "proj4d/render_geometry.hpp"
#include "proj4d/view_status.hpp"
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
  BlockCoord center{};
  std::uint64_t worldRevision{std::numeric_limits<std::uint64_t>::max()};
  bool initialized{};
  std::vector<FeatureEdge4D> edges;
};

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
  std::uint64_t savedRevision{};
  bool failureReported{};
};

bool initializePersistentWorld(BlockWorld &world,
                               PersistentWorldSession &session,
                               std::string &error) {
  char *preferencePath = SDL_GetPrefPath("Proj4D", "Proj4D");
  if (preferencePath == nullptr) {
    error =
        std::string("could not locate the save directory: ") + SDL_GetError();
    return false;
  }
  session.path = std::filesystem::path(preferencePath) / "worlds" /
                 worldSaveFilename(world.terrainMode());
  SDL_free(preferencePath);

  const WorldLoadStatus loadStatus = loadWorldSave(session.path, world, error);
  if (loadStatus == WorldLoadStatus::Error) {
    return false;
  }
  if (loadStatus == WorldLoadStatus::NotFound &&
      !saveWorldSave(session.path, world, error)) {
    return false;
  }
  session.savedRevision = world.revision();
  std::cout << (loadStatus == WorldLoadStatus::Loaded ? "Loaded " : "Created ")
            << terrainModeName(world.terrainMode()) << " world save\n";
  return true;
}

bool persistChangedWorld(const BlockWorld &world,
                         PersistentWorldSession &session) {
  if (world.revision() == session.savedRevision) {
    return true;
  }
  std::string error;
  if (!saveWorldSave(session.path, world, error)) {
    if (!session.failureReported) {
      std::cerr << "World save failed: " << error << '\n';
      session.failureReported = true;
    }
    return false;
  }
  session.savedRevision = world.revision();
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
               const std::array<std::uint8_t, 3> &color) {
  const auto from = projectForDisplay(line.from, display, width, height);
  const auto to = projectForDisplay(line.to, display, width, height);
  if (!from || !to) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], 255);
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

void drawStatusBar(SDL_Renderer *renderer, const Camera4D &camera, int width) {
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
            int height, FeatureGeometryCache &geometryCache) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  drawVisionCube(renderer, display, width, height);
  const BlockCoord center = containingBlock(camera.position);
  if (!geometryCache.initialized || geometryCache.center != center ||
      geometryCache.worldRevision != world.revision()) {
    geometryCache.center = center;
    geometryCache.worldRevision = world.revision();
    geometryCache.edges = buildFeatureEdges(world, center);
    geometryCache.initialized = true;
  }
  const std::vector<Line3> lines =
      projectVisibleFeatureEdges(world, geometryCache.edges, camera);
  constexpr std::array<std::array<std::uint8_t, 3>, 4> colors{{
      {245, 140, 140},
      {150, 245, 165},
      {135, 185, 255},
      {225, 150, 255},
  }};
  for (const Line3 &line : lines) {
    drawLine3(renderer, display, width, height, line,
              colors[static_cast<std::size_t>(line.worldAxis)]);
  }
  if (const auto hit = raycast(world, camera.position, camera.forward, 8.0)) {
    for (const Line3 &line :
         buildVisibleTesseractWireframe(world, hit->block, camera)) {
      drawLine3(renderer, display, width, height, line, {255, 255, 255});
    }
  }
  drawCrosshair(renderer, display, width, height);
  drawStatusBar(renderer, camera, width);
  SDL_RenderPresent(renderer);
}

void render(SDL_Renderer *renderer, const BlockWorld2D &world,
            const Camera2D &camera, int width, int height) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  const auto target = raycast(world, camera.position, camera.forward(), 8.0);
  const std::vector<VisionSample2D> samples =
      buildVisionLine(world, camera, height, camera.farPlane,
                      target ? std::optional(target->block) : std::nullopt);
  const int stripWidth = std::max(1, height / visionLineWidthDivisor);
  const int stripLeft = (width - stripWidth) / 2;
  int runStart = 0;
  while (runStart < height) {
    const VisionSample2D &sample =
        samples[static_cast<std::size_t>(height - 1 - runStart)];
    int runEnd = runStart + 1;
    while (runEnd < height) {
      const VisionSample2D &next =
          samples[static_cast<std::size_t>(height - 1 - runEnd)];
      if (next.solid != sample.solid || next.worldAxis != sample.worldAxis ||
          next.block != sample.block || next.targeted != sample.targeted) {
        break;
      }
      ++runEnd;
    }
    if (sample.solid) {
      const std::array<std::uint8_t, 3> color =
          sample.targeted ? std::array<std::uint8_t, 3>{255, 255, 255}
                          : visionBlockColor2D(sample.worldAxis, sample.block);
      SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], 255);
      const SDL_Rect rectangle{stripLeft, runStart, stripWidth,
                               runEnd - runStart};
      SDL_RenderFillRect(renderer, &rectangle);
    }
    runStart = runEnd;
  }

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderDrawLine(renderer, stripLeft, height / 2,
                     stripLeft + stripWidth - 1, height / 2);
  drawStatusBar(renderer, camera, width);
  SDL_RenderPresent(renderer);
}

struct VisionGeometry3D {
  BlockCoord3D center{};
  std::uint64_t worldRevision{std::numeric_limits<std::uint64_t>::max()};
  bool geometryInitialized{};
  std::vector<VisibleFace3D> faces;
};

ScreenPoint visionScreenPoint(const Vec2 &point, int width, int height) {
  return {static_cast<int>(std::lround((point.x + 1.0) * 0.5 * width)),
          static_cast<int>(std::lround((1.0 - point.y) * 0.5 * height))};
}

bool render(SDL_Renderer *renderer, const BlockWorld3D &world,
            const Camera3D &camera, int width, int height,
            VisionGeometry3D &vision) {
  const BlockCoord3D center = containingBlock(camera.position);
  if (!vision.geometryInitialized || vision.center != center ||
      vision.worldRevision != world.revision()) {
    vision.center = center;
    vision.worldRevision = world.revision();
    vision.faces = buildVisibleFaces3D(world, center);
    vision.geometryInitialized = true;
  }
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  const double aspectRatio =
      static_cast<double>(width) / static_cast<double>(height);
  std::vector<ProjectedFace3D> projected =
      projectVisibleFaces3D(vision.faces, camera, aspectRatio);
  std::ranges::sort(projected, std::greater{}, &ProjectedFace3D::depth);
  std::vector<SDL_Vertex> vertices;
  std::vector<int> indices;
  vertices.reserve(projected.size() * 4U);
  indices.reserve(projected.size() * 6U);
  for (const ProjectedFace3D &face : projected) {
    const int firstVertex = static_cast<int>(vertices.size());
    const auto color = visionBlockColor3D(face.worldAxis, face.block);
    for (std::size_t corner = 0; corner < face.vertexCount; ++corner) {
      const Vec2 &point = face.vertices[corner];
      SDL_Vertex vertex{};
      vertex.position.x = static_cast<float>((point.x + 1.0) * 0.5 * width);
      vertex.position.y = static_cast<float>((1.0 - point.y) * 0.5 * height);
      vertex.color = SDL_Color{color[0], color[1], color[2], 255};
      vertices.push_back(vertex);
    }
    for (std::size_t corner = 1; corner + 1 < face.vertexCount; ++corner) {
      indices.push_back(firstVertex);
      indices.push_back(firstVertex + static_cast<int>(corner));
      indices.push_back(firstVertex + static_cast<int>(corner + 1));
    }
  }
  if (!vertices.empty() &&
      SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                         static_cast<int>(vertices.size()), indices.data(),
                         static_cast<int>(indices.size())) != 0) {
    return false;
  }

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
  SDL_RenderPresent(renderer);
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
                               const std::string &smokeOutput) {
  const std::string worldName = terrainModeName(terrainMode);
  SDL_SetWindowTitle(window, ("Proj4D | 3D " + worldName +
                              " | Mouse: look | WASD: move | Shift: sneak")
                                 .c_str());
  BlockWorld sharedWorld(terrainMode);
  std::optional<PersistentWorldSession> persistence;
  if (!smokeTest) {
    persistence.emplace();
    std::string saveError;
    if (!initializePersistentWorld(sharedWorld, *persistence, saveError)) {
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
  PlayerMotionState3D playerMotion{0.0, true, spawnPosition};
  VisionGeometry3D vision;
  bool running = true;
  bool renderFailed = false;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D 3D " << worldName << " world (shared w=0 slice)\n"
              << "Controls:\n"
              << "  W/S: move forward/backward\n"
              << "  A/D: move left/right\n"
              << "  Mouse: look around\n"
              << "  Space: jump 1.5 blocks\n"
              << "  Hold Shift: sneak and avoid walking off edges\n"
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
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      } else if (event.type == SDL_MOUSEMOTION && !smokeTest) {
        camera.turnHorizontal(static_cast<double>(event.motion.xrel) *
                              mouseLookRadiansPerPixel);
        camera.turnVertical(-static_cast<double>(event.motion.yrel) *
                            mouseLookRadiansPerPixel);
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        bool worldChanged = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
          worldChanged =
              breakAlongRay(world, camera.position, camera.forward(), 8.0);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord3D, 2> protectedBlocks{{
              containingBlock(camera.position),
              playerLowerBodyBlock(camera.position,
                                   playerMotion.sneaking
                                       ? playerSneakCollisionBounds
                                       : playerCollisionBounds),
          }};
          worldChanged = buildAlongRay(world, camera.position, camera.forward(),
                                       8.0, protectedBlocks);
        }
        if (worldChanged && persistence) {
          static_cast<void>(persistChangedWorld(sharedWorld, *persistence));
        }
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
    updatePlayerMotion(camera, world, playerMotion, movement,
                       keys[SDL_SCANCODE_SPACE] != 0, deltaSeconds);

    int width = 1;
    int height = 1;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    if (!render(renderer, world, camera, width, height, vision)) {
      std::cerr << "Could not render the 3D view: " << SDL_GetError() << '\n';
      renderFailed = true;
      running = false;
      continue;
    }

    ++frameCount;
    if (!smokeTest && frameCount % 60 == 0) {
      const std::string title = "Proj4D | 3D " + worldName +
                                " | shared w=0 slice | loaded " +
                                std::to_string(world.loadedChunkCount()) +
                                " | WASD move | Mouse look | Shift sneak";
      SDL_SetWindowTitle(window, title.c_str());
    }
    if (smokeTest && frameCount >= 3) {
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save 3D smoke image: " << SDL_GetError()
                  << '\n';
        renderFailed = true;
      }
      running = false;
    }
  }

  const bool saved =
      !persistence || persistChangedWorld(sharedWorld, *persistence);
  return renderFailed || !saved ? 1 : 0;
}

int runTwoDimensionalSession(SDL_Renderer *renderer, SDL_Window *window,
                             TerrainMode terrainMode, bool smokeTest,
                             const std::string &smokeOutput) {
  const std::string worldName = terrainModeName(terrainMode);
  SDL_SetWindowTitle(window,
                     ("Proj4D | 2D " + worldName +
                      " | Mouse: vertical look | Z: reverse | Shift: sneak")
                         .c_str());
  BlockWorld sharedWorld(terrainMode);
  std::optional<PersistentWorldSession> persistence;
  if (!smokeTest) {
    persistence.emplace();
    std::string saveError;
    if (!initializePersistentWorld(sharedWorld, *persistence, saveError)) {
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
  PlayerMotionState2D playerMotion{0.0, true, spawnPosition};
  bool running = true;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D 2D " << worldName << " world\n"
              << "Controls:\n"
              << "  W/S: move forward/backward in 2D\n"
              << "  Mouse up/down: look up/down\n"
              << "  Z: reverse the horizontal viewing direction\n"
              << "  Space: jump 1.5 blocks\n"
              << "  Hold Shift: sneak and avoid walking off edges\n"
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
        running = false;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_z &&
                 event.key.repeat == 0) {
        camera.reverseHorizontalView();
      } else if (event.type == SDL_MOUSEMOTION && !smokeTest) {
        camera.turnVertical(-static_cast<double>(event.motion.yrel) *
                            mouseLookRadiansPerPixel);
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        bool worldChanged = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
          worldChanged =
              breakAlongRay(world, camera.position, camera.forward(), 8.0);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord2D, 2> protectedBlocks{{
              containingBlock(camera.position),
              playerLowerBodyBlock(camera.position,
                                   playerMotion.sneaking
                                       ? playerSneakCollisionBounds
                                       : playerCollisionBounds),
          }};
          worldChanged = buildAlongRay(world, camera.position, camera.forward(),
                                       8.0, protectedBlocks);
        }
        if (worldChanged && persistence) {
          static_cast<void>(persistChangedWorld(sharedWorld, *persistence));
        }
      }
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const PlayerMoveInput2D movement{
        (keys[SDL_SCANCODE_W] != 0 ? 1.0 : 0.0) -
            (keys[SDL_SCANCODE_S] != 0 ? 1.0 : 0.0),
        keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0,
    };
    updatePlayerMotion(camera, world, playerMotion, movement,
                       keys[SDL_SCANCODE_SPACE] != 0, deltaSeconds);

    int width = 1;
    int height = 1;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    render(renderer, world, camera, width, height);

    ++frameCount;
    if (!smokeTest && frameCount % 60 == 0) {
      const std::string title =
          "Proj4D | 2D " + worldName + " | shared 16^4 chunks | loaded " +
          std::to_string(world.loadedChunkCount()) +
          " | W/S move | Mouse: vertical | Z: reverse | Shift: sneak";
      SDL_SetWindowTitle(window, title.c_str());
    }
    if (smokeTest && frameCount >= 3) {
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save 2D smoke image: " << SDL_GetError()
                  << '\n';
        frameCount = -1;
      }
      running = false;
    }
  }
  const bool saved =
      !persistence || persistChangedWorld(sharedWorld, *persistence);
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
                                   mode == RunMode::TwoDHighSmokeTest;
  const bool threeDimensionalSmoke =
      mode == RunMode::ThreeDTerrainMenuSmokeTest ||
      mode == RunMode::ThreeDFlatSmokeTest ||
      mode == RunMode::ThreeDLowSmokeTest ||
      mode == RunMode::ThreeDHighSmokeTest;
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
        renderer, window, *selectedTerrain, smokeTest, smokeOutput);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
  }

  if (*selectedDimension == WorldDimension::Three) {
    const int result = runThreeDimensionalSession(
        renderer, window, *selectedTerrain, smokeTest, smokeOutput);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
  }

  const std::string worldName = terrainModeName(*selectedTerrain);
  SDL_SetWindowTitle(window, ("Proj4D | " + worldName +
                              " World | Mouse: 4D look | Tab: vertical | "
                              "Ctrl: orbit | Shift: sneak | WASDQE move")
                                 .c_str());
  BlockWorld world(*selectedTerrain);
  std::optional<PersistentWorldSession> persistence;
  if (!smokeTest) {
    persistence.emplace();
    std::string saveError;
    if (!initializePersistentWorld(world, *persistence, saveError)) {
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
  DisplayCamera display;
  FeatureGeometryCache geometryCache;
  PlayerMotionState playerMotion{0.0, true, spawnPosition};
  bool running = true;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D " << worldName << " world\n"
              << "Controls:\n"
              << "  W/S: move forward/backward in 4D\n"
              << "  A/D: move along the first sideways direction\n"
              << "  Q/E: move along the second sideways direction\n"
              << "  Space: jump 1.5 blocks\n"
              << "  Hold Shift: sneak and avoid walking off edges\n"
              << "  Up/Down: look up/down (limited to straight up/down)\n"
              << "  Mouse left/right: ordinary horizontal look\n"
              << "  Mouse up/down: fourth-dimensional look\n"
              << "  Hold Tab + mouse up/down: vertical look\n"
              << "  Hold Ctrl + mouse: orbit the solid 3D vision cube\n"
              << "  Mouse wheel: zoom the vision cube\n"
              << "  Left click: break a targeted tesseract\n"
              << "  Right click: build a tesseract\n"
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
        running = false;
      } else if (event.type == SDL_MOUSEMOTION && !smokeTest) {
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
        bool worldChanged = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
          worldChanged =
              breakAlongRay(world, camera.position, camera.forward, 8.0);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord, 2> protectedBlocks{{
              containingBlock(camera.position),
              playerLowerBodyBlock(camera.position,
                                   playerMotion.sneaking
                                       ? playerSneakCollisionBounds
                                       : playerCollisionBounds),
          }};
          worldChanged = buildAlongRay(world, camera.position, camera.forward,
                                       8.0, protectedBlocks);
        }
        if (worldChanged && persistence) {
          static_cast<void>(persistChangedWorld(world, *persistence));
        }
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
    updatePlayerMotion(camera, world, playerMotion, movement,
                       keys[SDL_SCANCODE_SPACE] != 0, deltaSeconds);

    int width = initialWidth;
    int height = initialHeight;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    render(renderer, world, camera, display, width, height, geometryCache);

    ++frameCount;
    if (!smokeTest && frameCount % 60 == 0) {
      const std::string title = "Proj4D | " + worldName +
                                " | infinite 16^4 chunks | loaded " +
                                std::to_string(world.loadedChunkCount()) +
                                " | WASDQE move | Mouse: 4D look | "
                                "Tab: vertical | Shift: sneak | "
                                "Ctrl: orbit";
      SDL_SetWindowTitle(window, title.c_str());
    }
    if (smokeTest && frameCount >= 3) {
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save smoke image: " << SDL_GetError() << '\n';
        frameCount = -1;
      }
      running = false;
    }
  }

  const bool saved = !persistence || persistChangedWorld(world, *persistence);

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return frameCount < 0 || !saved ? 1 : 0;
}

} // namespace proj4d
