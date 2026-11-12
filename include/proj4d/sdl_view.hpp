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
  ThreeDTerrainMenuSmokeTest,
  TwoDTerrainMenuSmokeTest,
  ThreeDFlatSmokeTest,
  ThreeDLowSmokeTest,
  ThreeDHighSmokeTest,
  TwoDFlatSmokeTest,
  TwoDLowSmokeTest,
  TwoDHighSmokeTest,
  FourDInventorySmokeTest,
  FourDSliceSmokeTest,
  ThreeDInventorySmokeTest,
  TwoDInventorySmokeTest,
  FourDFluidSmokeTest,
  ThreeDFluidSmokeTest,
  TwoDFluidSmokeTest,
};

int runApplication(RunMode mode, const std::string &smokeOutput);

} // namespace proj4d
