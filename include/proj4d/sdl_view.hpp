#pragma once

#include <string>

namespace proj4d {

enum class RunMode {
  Interactive,
  FlatSmokeTest,
  LowSmokeTest,
  HighSmokeTest,
  MenuSmokeTest,
  FourDTerrainMenuSmokeTest,
  TwoDTerrainMenuSmokeTest,
  TwoDFlatSmokeTest,
  TwoDLowSmokeTest,
  TwoDHighSmokeTest,
};

int runApplication(RunMode mode, const std::string &smokeOutput);

} // namespace proj4d
