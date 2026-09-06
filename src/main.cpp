#include "proj4d/sdl_view.hpp"

#include <iostream>
#include <string>

int main(int argc, char **argv) {
  proj4d::RunMode mode = proj4d::RunMode::Interactive;
  std::string smokeOutput = "proj4d-smoke.bmp";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--smoke-test") {
      mode = proj4d::RunMode::FlatSmokeTest;
    } else if (argument == "--low-smoke-test") {
      mode = proj4d::RunMode::LowSmokeTest;
    } else if (argument == "--high-smoke-test" ||
               argument == "--normal-smoke-test") {
      mode = proj4d::RunMode::HighSmokeTest;
    } else if (argument == "--menu-smoke-test") {
      mode = proj4d::RunMode::MenuSmokeTest;
    } else if (argument == "--4d-terrain-menu-smoke-test") {
      mode = proj4d::RunMode::FourDTerrainMenuSmokeTest;
    } else if (argument == "--3d-terrain-menu-smoke-test") {
      mode = proj4d::RunMode::ThreeDTerrainMenuSmokeTest;
    } else if (argument == "--2d-terrain-menu-smoke-test") {
      mode = proj4d::RunMode::TwoDTerrainMenuSmokeTest;
    } else if (argument == "--3d-smoke-test") {
      mode = proj4d::RunMode::ThreeDFlatSmokeTest;
    } else if (argument == "--3d-low-smoke-test") {
      mode = proj4d::RunMode::ThreeDLowSmokeTest;
    } else if (argument == "--3d-high-smoke-test") {
      mode = proj4d::RunMode::ThreeDHighSmokeTest;
    } else if (argument == "--2d-smoke-test") {
      mode = proj4d::RunMode::TwoDFlatSmokeTest;
    } else if (argument == "--2d-low-smoke-test") {
      mode = proj4d::RunMode::TwoDLowSmokeTest;
    } else if (argument == "--2d-high-smoke-test") {
      mode = proj4d::RunMode::TwoDHighSmokeTest;
    } else if (argument == "--smoke-output" && index + 1 < argc) {
      smokeOutput = argv[++index];
    } else if (argument == "--help") {
      std::cout << "Proj4D - true projected 4D, 3D, and 2D block worlds\n\n"
                << "Usage: proj4d [--smoke-test | --low-smoke-test | "
                   "--high-smoke-test | --3d-smoke-test | "
                   "--3d-low-smoke-test | --3d-high-smoke-test | "
                   "--2d-smoke-test | "
                   "--2d-low-smoke-test | --2d-high-smoke-test | "
                   "--menu-smoke-test | --4d-terrain-menu-smoke-test | "
                   "--3d-terrain-menu-smoke-test | "
                   "--2d-terrain-menu-smoke-test]\n"
                << "              [--smoke-output FILE]\n"
                << "Alias: --normal-smoke-test selects 4D High terrain.\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 2;
    }
  }
  return proj4d::runApplication(mode, smokeOutput);
}
