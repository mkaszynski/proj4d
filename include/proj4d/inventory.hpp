#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "proj4d/content.hpp"

namespace proj4d {

enum class PlayDimension : int {
  Two = 2,
  Three = 3,
  Four = 4,
};

enum class CraftingStation {
  Hand,
  Table,
};

struct ItemStack {
  BlockId item{BlockId::air};
  int count{};
  int durability{};

  [[nodiscard]] bool empty() const;
  [[nodiscard]] bool operator==(const ItemStack &) const = default;
};

inline constexpr int storageSide = 4;
inline constexpr int storageSlotCount = storageSide * storageSide * storageSide;
inline constexpr int quickSide = 3;
inline constexpr int quickSlotCount = quickSide * quickSide * quickSide;
inline constexpr int handCraftSide = 2;
inline constexpr int handCraftSlotCount =
    handCraftSide * handCraftSide * handCraftSide;
inline constexpr int tableCraftSide = 3;
inline constexpr int tableCraftSlotCount =
    tableCraftSide * tableCraftSide * tableCraftSide;

struct InventoryState {
  std::array<ItemStack, storageSlotCount> storage{};
  std::array<ItemStack, quickSlotCount> quick{};
  std::array<ItemStack, handCraftSlotCount> handCrafting{};
  std::array<ItemStack, tableCraftSlotCount> tableCrafting{};
  ItemStack cursor{};
  int selectedQuick{};
};

[[nodiscard]] ItemStack normalizedStack(ItemStack stack);
[[nodiscard]] std::vector<std::size_t> visibleStorageSlots(PlayDimension mode);
[[nodiscard]] std::vector<std::size_t> visibleQuickSlots(PlayDimension mode);
[[nodiscard]] std::vector<std::size_t>
visibleCraftingSlots(PlayDimension mode, CraftingStation station);
[[nodiscard]] std::span<ItemStack> craftingSlots(InventoryState &inventory,
                                                 CraftingStation station);
[[nodiscard]] std::span<const ItemStack>
craftingSlots(const InventoryState &inventory, CraftingStation station);
[[nodiscard]] ItemStack craftingOutput(const InventoryState &inventory,
                                       PlayDimension mode,
                                       CraftingStation station);
[[nodiscard]] bool takeCraftingOutput(InventoryState &inventory,
                                      PlayDimension mode,
                                      CraftingStation station);
[[nodiscard]] bool addItem(InventoryState &inventory, ItemStack stack,
                           PlayDimension mode = PlayDimension::Four);
[[nodiscard]] bool consumeSelectedItem(InventoryState &inventory);
[[nodiscard]] bool damageSelectedTool(InventoryState &inventory,
                                      int amount = 1);
void selectQuickSlot(InventoryState &inventory, PlayDimension mode,
                     int visibleIndex);
void clickStack(ItemStack &slot, ItemStack &cursor, int mouseButton);

} // namespace proj4d
