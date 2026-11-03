#pragma once

#include <optional>
#include <span>

#include "proj4d/inventory.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

struct MiningProgress {
  std::optional<BlockCoord> target;
  double seconds{};
  double requiredSeconds{};
};

class GameSession {
public:
  explicit GameSession(TerrainMode terrainMode, std::uint32_t seed = 0x4D2026U,
                       std::size_t maximumLoadedChunks = 96U);
  GameSession(TerrainMode terrainMode, PlayDimension dimension,
              std::uint32_t seed = 0x4D2026U,
              std::size_t maximumLoadedChunks = 96U);

  [[nodiscard]] BlockWorld &world();
  [[nodiscard]] const BlockWorld &world() const;
  [[nodiscard]] InventoryState &inventory();
  [[nodiscard]] const InventoryState &inventory() const;
  [[nodiscard]] const MiningProgress &mining() const;
  [[nodiscard]] std::uint64_t inventoryRevision() const;
  void replaceInventory(InventoryState inventory);

  [[nodiscard]] bool updateMining(std::optional<BlockCoord> target, bool active,
                                  double deltaSeconds);
  void cancelMining();
  [[nodiscard]] bool
  placeSelected(const BlockCoord &coordinate,
                std::span<const BlockCoord> protectedBlocks = {});

private:
  [[nodiscard]] ItemStack selectedStack() const;
  [[nodiscard]] bool canCollect(BlockId material) const;

  BlockWorld world_;
  PlayDimension dimension_{PlayDimension::Four};
  InventoryState inventory_;
  MiningProgress mining_;
  std::uint64_t inventoryRevision_{};
};

[[nodiscard]] double miningSeconds(BlockId material, BlockId tool);

} // namespace proj4d
