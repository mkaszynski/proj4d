#include "proj4d/sdl_view.hpp"

#include <iostream>
#include <string>

int main(int argc, char **argv) {
  bool smokeTest = false;
  std::string smokeOutput = "proj4d-smoke.bmp";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--smoke-test") {
      smokeTest = true;
    } else if (argument == "--smoke-output" && index + 1 < argc) {
      smokeOutput = argv[++index];
    } else if (argument == "--help") {
      std::cout << "Proj4D - a true projected 4D block world\n\n"
                << "Usage: proj4d [--smoke-test] [--smoke-output FILE]\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 2;
    }
  }
  return proj4d::runApplication(smokeTest, smokeOutput);
}
