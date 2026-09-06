#pragma once

#include <filesystem>
#include <string>

#include "proj4d/world.hpp"

namespace proj4d {

enum class WorldLoadStatus {
  Loaded,
  NotFound,
  Error,
};

[[nodiscard]] std::filesystem::path worldSaveFilename(TerrainMode mode);
[[nodiscard]] WorldLoadStatus loadWorldSave(const std::filesystem::path &path,
                                            BlockWorld &world,
                                            std::string &error);
[[nodiscard]] bool saveWorldSave(const std::filesystem::path &path,
                                 const BlockWorld &world, std::string &error);

} // namespace proj4d
