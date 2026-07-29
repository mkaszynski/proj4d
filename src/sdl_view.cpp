#include "proj4d/sdl_view.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "proj4d/camera.hpp"
#include "proj4d/render_geometry.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double eyeHeight = 1.1;

struct DisplayCamera {
  double yaw{-0.58};
  double pitch{0.42};
  double distance{3.8};
};

struct ScreenPoint {
  int x{};
  int y{};
};

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

void render(SDL_Renderer *renderer, const BlockWorld &world,
            const Camera4D &camera, const DisplayCamera &display, int width,
            int height) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  drawVisionCube(renderer, display, width, height);
  const std::vector<Line3> lines = buildVisionGeometry(world, camera);
  constexpr std::array<std::array<std::uint8_t, 3>, 4> colors{{
      {245, 140, 140},
      {150, 245, 165},
      {135, 185, 255},
      {225, 150, 255},
  }};
  for (const Line3 &line : lines) {
    drawLine3(renderer, display, width, height, line,
              colors[static_cast<std::size_t>(line.boundaryAxis)]);
  }
  if (const auto hit = raycast(world, camera.position, camera.forward, 8.0)) {
    for (const Line3 &line : buildTesseractWireframe(hit->block, camera)) {
      drawLine3(renderer, display, width, height, line, {255, 255, 255});
    }
  }
  drawCrosshair(renderer, display, width, height);
  SDL_RenderPresent(renderer);
}

void updatePlayer(Camera4D &camera, const BlockWorld &world,
                  double &verticalVelocity, bool &grounded, double moveInput,
                  double deltaSeconds) {
  constexpr double movementSpeed = 2.4;
  constexpr double gravity = -8.5;
  if (moveInput != 0.0) {
    const Vec4 movement =
        camera.flattenedForward() * (moveInput * movementSpeed * deltaSeconds);
    Vec4 candidate = camera.position + movement;
    const WorldBounds &bounds = world.bounds();
    candidate.x =
        std::clamp(candidate.x, static_cast<double>(bounds.minimum.x) - 0.5,
                   static_cast<double>(bounds.maximum.x) + 1.5);
    candidate.z =
        std::clamp(candidate.z, static_cast<double>(bounds.minimum.z) - 0.5,
                   static_cast<double>(bounds.maximum.z) + 1.5);
    candidate.w =
        std::clamp(candidate.w, static_cast<double>(bounds.minimum.w) - 0.5,
                   static_cast<double>(bounds.maximum.w) + 1.5);
    if (!world.isSolid(containingBlock(candidate)) &&
        !world.isSolid(containingBlock({candidate.x, candidate.y - eyeHeight,
                                        candidate.z, candidate.w}))) {
      camera.position = candidate;
    }
  }

  verticalVelocity += gravity * deltaSeconds;
  camera.position.y += verticalVelocity * deltaSeconds;
  const BlockCoord below = containingBlock({
      camera.position.x,
      camera.position.y - eyeHeight - 0.02,
      camera.position.z,
      camera.position.w,
  });
  if (verticalVelocity <= 0.0 && world.isSolid(below)) {
    camera.position.y = static_cast<double>(below.y + 1) + eyeHeight;
    verticalVelocity = 0.0;
    grounded = true;
  } else {
    grounded = false;
  }
  if (camera.position.y < static_cast<double>(world.bounds().minimum.y) - 4.0) {
    camera.position = {0.5, 2.1, -3.5, 0.5};
    verticalVelocity = 0.0;
  }
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

} // namespace

int runApplication(bool smokeTest, const std::string &smokeOutput) {
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
  SDL_Window *window =
      SDL_CreateWindow("Proj4D | W/S move | Space jump | A/D + arrows + Q/E "
                       "look | LMB break | RMB build",
                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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

  BlockWorld world;
  world.fillFlatGround();
  Camera4D camera;
  camera.turnVertical(-0.28);
  DisplayCamera display;
  double verticalVelocity = 0.0;
  bool grounded = true;
  bool running = true;
  std::uint64_t previousCounter = SDL_GetPerformanceCounter();

  if (!smokeTest) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    std::cout << "Proj4D controls:\n"
              << "  W/S: move forward/backward in 4D\n"
              << "  Space: jump\n"
              << "  A/D: turn left/right\n"
              << "  Up/Down: look up/down\n"
              << "  Q/E: turn through the fourth dimension\n"
              << "  Mouse: orbit the solid 3D vision cube\n"
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
        display.yaw += static_cast<double>(event.motion.xrel) * 0.004;
        display.pitch = std::clamp(
            display.pitch + static_cast<double>(event.motion.yrel) * 0.004,
            -pi * 0.48, pi * 0.48);
      } else if (event.type == SDL_MOUSEWHEEL) {
        display.distance = std::clamp(
            display.distance - static_cast<double>(event.wheel.y) * 0.2, 2.6,
            6.5);
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          static_cast<void>(
              breakAlongRay(world, camera.position, camera.forward, 8.0));
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          const std::array<BlockCoord, 2> protectedBlocks{{
              containingBlock(camera.position),
              containingBlock({
                  camera.position.x,
                  camera.position.y - eyeHeight,
                  camera.position.z,
                  camera.position.w,
              }),
          }};
          static_cast<void>(buildAlongRay(
              world, camera.position, camera.forward, 8.0, protectedBlocks));
        }
      } else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_SPACE && grounded) {
        verticalVelocity = 4.25;
        grounded = false;
      }
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const double movement = (keys[SDL_SCANCODE_W] != 0 ? 1.0 : 0.0) -
                            (keys[SDL_SCANCODE_S] != 0 ? 1.0 : 0.0);
    constexpr double turnSpeed = 1.25;
    camera.turnHorizontal(((keys[SDL_SCANCODE_D] != 0 ? 1.0 : 0.0) -
                           (keys[SDL_SCANCODE_A] != 0 ? 1.0 : 0.0)) *
                          turnSpeed * deltaSeconds);
    camera.turnVertical(((keys[SDL_SCANCODE_UP] != 0 ? 1.0 : 0.0) -
                         (keys[SDL_SCANCODE_DOWN] != 0 ? 1.0 : 0.0)) *
                        turnSpeed * deltaSeconds);
    camera.turnFourth(((keys[SDL_SCANCODE_E] != 0 ? 1.0 : 0.0) -
                       (keys[SDL_SCANCODE_Q] != 0 ? 1.0 : 0.0)) *
                      turnSpeed * deltaSeconds);
    updatePlayer(camera, world, verticalVelocity, grounded, movement,
                 deltaSeconds);

    int width = initialWidth;
    int height = initialHeight;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    render(renderer, world, camera, display, width, height);

    ++frameCount;
    if (smokeTest && frameCount >= 3) {
      if (!saveRendererBitmap(renderer, width, height, smokeOutput)) {
        std::cerr << "Could not save smoke image: " << SDL_GetError() << '\n';
        frameCount = -1;
      }
      running = false;
    }
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return frameCount < 0 ? 1 : 0;
}

} // namespace proj4d
