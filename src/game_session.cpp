#include "proj4d/game_session.hpp"

#include <algorithm>

namespace proj4d {

double miningSeconds(BlockId material, BlockId tool) {
  if (material == BlockId::air || !isRenderable(material)) {
    return 0.0;
  }
  constexpr double baseSeconds = 2.0;
  return baseSeconds * miningMultiplier(material) /
         std::max(0.001, toolSpeed(tool));
}

GameSession::GameSession(TerrainMode terrainMode, std::uint32_t seed,
                         std::size_t maximumLoadedChunks)
    : GameSession(terrainMode, PlayDimension::Four, seed, maximumLoadedChunks) {
}

GameSession::GameSession(TerrainMode terrainMode, PlayDimension dimension,
                         std::uint32_t seed, std::size_t maximumLoadedChunks)
    : world_(terrainMode, seed, maximumLoadedChunks), dimension_(dimension) {
  inventory_.selectedQuick =
      static_cast<int>(visibleQuickSlots(dimension_).front());
}

BlockWorld &GameSession::world() { return world_; }

const BlockWorld &GameSession::world() const { return world_; }

InventoryState &GameSession::inventory() { return inventory_; }

const InventoryState &GameSession::inventory() const { return inventory_; }

const MiningProgress &GameSession::mining() const { return mining_; }

std::uint64_t GameSession::inventoryRevision() const {
  return inventoryRevision_;
}

void GameSession::replaceInventory(InventoryState inventory) {
  const std::vector<std::size_t> visible = visibleQuickSlots(dimension_);
  if (std::find(visible.begin(), visible.end(),
                static_cast<std::size_t>(
                    std::max(0, inventory.selectedQuick))) == visible.end()) {
    inventory.selectedQuick = static_cast<int>(visible.front());
  }
  inventory_ = std::move(inventory);
  ++inventoryRevision_;
}

ItemStack GameSession::selectedStack() const {
  return inventory_.quick.at(static_cast<std::size_t>(
      std::clamp(inventory_.selectedQuick, 0, quickSlotCount - 1)));
}

bool GameSession::canCollect(BlockId material) const {
  InventoryState copy = inventory_;
  return addItem(copy, {material, 1, maximumDurability(material)}, dimension_);
}

bool GameSession::updateMining(std::optional<BlockCoord> target, bool active,
                               double deltaSeconds) {
  if (!active || !target) {
    cancelMining();
    return false;
  }
  const BlockId material = world_.blockAt(*target);
  if (!isRenderable(material) || hypercraft::is_fluid(material) ||
      !canCollect(material)) {
    cancelMining();
    return false;
  }
  const ItemStack tool = selectedStack();
  const BlockId toolId = tool.empty() ? BlockId::air : tool.item;
  if (mining_.target != target) {
    mining_.target = target;
    mining_.seconds = 0.0;
    mining_.requiredSeconds = miningSeconds(material, toolId);
  }
  mining_.seconds += std::max(0.0, deltaSeconds);
  if (mining_.seconds + 1.0e-9 < mining_.requiredSeconds) {
    return false;
  }
  if (!world_.setBlock(*target, BlockId::air) ||
      !addItem(inventory_, {material, 1, maximumDurability(material)},
               dimension_)) {
    cancelMining();
    return false;
  }
  if (isTool(toolId)) {
    static_cast<void>(damageSelectedTool(inventory_));
  }
  ++inventoryRevision_;
  cancelMining();
  return true;
}

void GameSession::cancelMining() { mining_ = {}; }

bool GameSession::placeSelected(const BlockCoord &coordinate,
                                std::span<const BlockCoord> protectedBlocks) {
  if (std::find(protectedBlocks.begin(), protectedBlocks.end(), coordinate) !=
          protectedBlocks.end() ||
      world_.isSolid(coordinate)) {
    return false;
  }
  const ItemStack selected = selectedStack();
  if (selected.empty() || !isPlaceable(selected.item) ||
      !world_.setBlock(coordinate, selected.item)) {
    return false;
  }
  static_cast<void>(consumeSelectedItem(inventory_));
  ++inventoryRevision_;
  return true;
}

} // namespace proj4d
