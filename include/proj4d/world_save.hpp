#pragma once

#include <filesystem>
#include <string>

#include "proj4d/inventory.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

enum class WorldLoadStatus {
  Loaded,
  NotFound,
  Error,
};

[[nodiscard]] std::filesystem::path worldSaveFilename(TerrainMode mode);
[[nodiscard]] WorldLoadStatus
loadWorldSave(const std::filesystem::path &path, BlockWorld &world,
              std::string &error, InventoryState *inventory = nullptr);
[[nodiscard]] bool saveWorldSave(const std::filesystem::path &path,
                                 const BlockWorld &world, std::string &error,
                                 const InventoryState *inventory = nullptr);

} // namespace proj4d
