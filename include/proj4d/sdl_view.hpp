#pragma once

#include <string>

namespace proj4d {

enum class RunMode {
  Interactive,
  FlatSmokeTest,
  LowSmokeTest,
  NormalSmokeTest,
  MenuSmokeTest,
};

int runApplication(RunMode mode, const std::string &smokeOutput);

} // namespace proj4d
