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
    } else if (argument == "--normal-smoke-test") {
      mode = proj4d::RunMode::NormalSmokeTest;
    } else if (argument == "--menu-smoke-test") {
      mode = proj4d::RunMode::MenuSmokeTest;
    } else if (argument == "--smoke-output" && index + 1 < argc) {
      smokeOutput = argv[++index];
    } else if (argument == "--help") {
      std::cout << "Proj4D - a true projected 4D block world\n\n"
                << "Usage: proj4d [--smoke-test | --low-smoke-test | "
                   "--normal-smoke-test | --menu-smoke-test]\n"
                << "              [--smoke-output FILE]\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 2;
    }
  }
  return proj4d::runApplication(mode, smokeOutput);
}
