#include "proj4d/inventory.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace proj4d {

namespace {

struct GridPoint {
  int x{};
  int y{};
  int z{};
};

enum class IngredientTag {
  Exact,
  Log,
  Planks,
};

struct Ingredient {
  GridPoint point{};
  BlockId item{BlockId::air};
  IngredientTag tag{IngredientTag::Exact};
};

struct Recipe {
  std::vector<Ingredient> ingredients;
  ItemStack output{};
};

constexpr std::size_t index3(int x, int y, int z, int side) {
  return static_cast<std::size_t>(x + side * (y + side * z));
}

std::vector<std::size_t> centeredSliceIndices(int side, int rank) {
  std::vector<std::size_t> indices;
  const int fixed = (side - 1) / 2;
  const int yLimit = rank >= 2 ? side : 1;
  const int zLimit = rank >= 3 ? side : 1;
  indices.reserve(static_cast<std::size_t>(side * yLimit * zLimit));
  for (int z = 0; z < zLimit; ++z) {
    for (int y = 0; y < yLimit; ++y) {
      for (int x = 0; x < side; ++x) {
        indices.push_back(
            index3(x, rank >= 2 ? y : fixed, rank >= 3 ? z : fixed, side));
      }
    }
  }
  return indices;
}

int interfaceRank(PlayDimension mode) { return static_cast<int>(mode) - 1; }

Ingredient exact(int x, int y, int z, BlockId item) {
  return {{x, y, z}, item, IngredientTag::Exact};
}

Ingredient tagged(int x, int y, int z, IngredientTag tag) {
  return {{x, y, z}, BlockId::air, tag};
}

ItemStack output(BlockId item, int count = 1) {
  return normalizedStack({item, count, maximumDurability(item)});
}

Recipe lineRecipe(std::initializer_list<Ingredient> ingredients,
                  ItemStack result) {
  return {ingredients, result};
}

std::vector<Recipe> recipesForRank(int rank) {
  std::vector<Recipe> recipes;
  const std::array<std::pair<BlockId, BlockId>, 4> plankRecipes{{
      {BlockId::pine_wood, BlockId::pine_planks},
      {BlockId::jungle_wood, BlockId::jungle_planks},
      {BlockId::windswept_wood, BlockId::windswept_planks},
      {BlockId::wood, BlockId::planks},
  }};
  for (const auto &[log, planks] : plankRecipes) {
    recipes.push_back(lineRecipe({exact(0, 0, 0, log)}, output(planks, 4)));
  }
  const GridPoint second = rank == 1 ? GridPoint{1, 0, 0} : GridPoint{0, 1, 0};
  recipes.push_back(
      {{tagged(0, 0, 0, IngredientTag::Planks),
        tagged(second.x, second.y, second.z, IngredientTag::Planks)},
       output(BlockId::sticks, 4)});
  recipes.push_back({{exact(0, 0, 0, BlockId::coal_ore),
                      exact(second.x, second.y, second.z, BlockId::sticks)},
                     output(BlockId::torch, 4)});

  if (rank == 1) {
    recipes.push_back(lineRecipe({tagged(0, 0, 0, IngredientTag::Planks),
                                  tagged(1, 0, 0, IngredientTag::Planks),
                                  tagged(2, 0, 0, IngredientTag::Planks)},
                                 output(BlockId::crafting_table)));
  } else {
    std::vector<Ingredient> table;
    const int depth = rank == 3 ? 2 : 1;
    for (int z = 0; z < depth; ++z) {
      for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
          table.push_back(tagged(x, y, z, IngredientTag::Planks));
        }
      }
    }
    recipes.push_back({std::move(table), output(BlockId::crafting_table)});
  }

  const std::array<std::pair<BlockId, BlockId>, 5> tools{{
      {BlockId::planks, BlockId::wood_tool},
      {BlockId::stone, BlockId::stone_tool},
      {BlockId::tin_bar, BlockId::tin_tool},
      {BlockId::copper_bar, BlockId::copper_tool},
      {BlockId::iron_bar, BlockId::iron_tool},
  }};
  for (const auto &[material, tool] : tools) {
    std::vector<Ingredient> pattern;
    if (rank == 1) {
      pattern = {exact(0, 0, 0, material), exact(1, 0, 0, BlockId::sticks),
                 exact(2, 0, 0, BlockId::sticks)};
    } else if (rank == 2) {
      for (int x = 0; x < 3; ++x) {
        pattern.push_back(exact(x, 0, 0, material));
      }
      pattern.push_back(exact(1, 1, 0, BlockId::sticks));
      pattern.push_back(exact(1, 2, 0, BlockId::sticks));
    } else {
      for (int z = 0; z < 3; ++z) {
        for (int x = 0; x < 3; ++x) {
          pattern.push_back(exact(x, 0, z, material));
        }
      }
      pattern.push_back(exact(1, 1, 1, BlockId::sticks));
      pattern.push_back(exact(1, 2, 1, BlockId::sticks));
    }
    recipes.push_back({std::move(pattern), output(tool)});
  }
  return recipes;
}

bool ingredientMatches(const Ingredient &ingredient, BlockId item) {
  if (ingredient.tag == IngredientTag::Log) {
    return hypercraft::is_log_block(item);
  }
  if (ingredient.tag == IngredientTag::Planks) {
    return hypercraft::is_planks_block(item);
  }
  return ingredient.item == item;
}

struct RecipeMatch {
  Recipe recipe;
  std::vector<std::size_t> occupied;
};

std::optional<RecipeMatch> matchRecipe(std::span<const ItemStack> grid,
                                       int side, int rank) {
  const int fixed = (side - 1) / 2;
  for (const Recipe &recipe : recipesForRank(rank)) {
    int extentX = 0;
    int extentY = 0;
    int extentZ = 0;
    for (const Ingredient &ingredient : recipe.ingredients) {
      extentX = std::max(extentX, ingredient.point.x + 1);
      extentY = std::max(extentY, ingredient.point.y + 1);
      extentZ = std::max(extentZ, ingredient.point.z + 1);
    }
    const int yOffset = rank >= 2 ? 0 : fixed;
    const int zOffset = rank >= 3 ? 0 : fixed;
    const int yTranslations = rank >= 2 ? side - extentY + 1 : 1;
    const int zTranslations = rank >= 3 ? side - extentZ + 1 : 1;
    for (int dz = 0; dz < zTranslations; ++dz) {
      for (int dy = 0; dy < yTranslations; ++dy) {
        for (int dx = 0; dx <= side - extentX; ++dx) {
          bool matches = true;
          std::vector<std::size_t> occupied;
          occupied.reserve(recipe.ingredients.size());
          for (const Ingredient &ingredient : recipe.ingredients) {
            const int x = ingredient.point.x + dx;
            const int y = ingredient.point.y + dy + yOffset;
            const int z = ingredient.point.z + dz + zOffset;
            const std::size_t index = index3(x, y, z, side);
            if (grid[index].empty() ||
                !ingredientMatches(ingredient, grid[index].item)) {
              matches = false;
              break;
            }
            occupied.push_back(index);
          }
          if (!matches) {
            continue;
          }
          const std::vector<std::size_t> visible =
              centeredSliceIndices(side, rank);
          for (const std::size_t index : visible) {
            if (!grid[index].empty() &&
                std::find(occupied.begin(), occupied.end(), index) ==
                    occupied.end()) {
              matches = false;
              break;
            }
          }
          if (matches) {
            return RecipeMatch{recipe, std::move(occupied)};
          }
        }
      }
    }
  }
  return std::nullopt;
}

} // namespace

bool ItemStack::empty() const { return item == BlockId::air || count <= 0; }

ItemStack normalizedStack(ItemStack stack) {
  if (stack.item == BlockId::air || stack.count <= 0 ||
      !isKnownBlock(stack.item)) {
    return {};
  }
  stack.count = std::clamp(stack.count, 1, maximumStackSize(stack.item));
  stack.durability =
      isTool(stack.item)
          ? std::clamp(stack.durability, 1, maximumDurability(stack.item))
          : 0;
  return stack;
}

std::vector<std::size_t> visibleStorageSlots(PlayDimension mode) {
  return centeredSliceIndices(storageSide, interfaceRank(mode));
}

std::vector<std::size_t> visibleQuickSlots(PlayDimension mode) {
  return centeredSliceIndices(quickSide, interfaceRank(mode));
}

std::vector<std::size_t> visibleCraftingSlots(PlayDimension mode,
                                              CraftingStation station) {
  const int side =
      station == CraftingStation::Hand ? handCraftSide : tableCraftSide;
  return centeredSliceIndices(side, interfaceRank(mode));
}

std::span<ItemStack> craftingSlots(InventoryState &inventory,
                                   CraftingStation station) {
  return station == CraftingStation::Hand
             ? std::span<ItemStack>(inventory.handCrafting)
             : std::span<ItemStack>(inventory.tableCrafting);
}

std::span<const ItemStack> craftingSlots(const InventoryState &inventory,
                                         CraftingStation station) {
  return station == CraftingStation::Hand
             ? std::span<const ItemStack>(inventory.handCrafting)
             : std::span<const ItemStack>(inventory.tableCrafting);
}

ItemStack craftingOutput(const InventoryState &inventory, PlayDimension mode,
                         CraftingStation station) {
  const int side =
      station == CraftingStation::Hand ? handCraftSide : tableCraftSide;
  const auto match =
      matchRecipe(craftingSlots(inventory, station), side, interfaceRank(mode));
  return match ? match->recipe.output : ItemStack{};
}

bool takeCraftingOutput(InventoryState &inventory, PlayDimension mode,
                        CraftingStation station) {
  const int side =
      station == CraftingStation::Hand ? handCraftSide : tableCraftSide;
  std::span<ItemStack> grid = craftingSlots(inventory, station);
  const auto match = matchRecipe(grid, side, interfaceRank(mode));
  if (!match) {
    return false;
  }
  const ItemStack result = match->recipe.output;
  if (!inventory.cursor.empty() &&
      (inventory.cursor.item != result.item || isTool(result.item) ||
       inventory.cursor.count + result.count > maximumStackSize(result.item))) {
    return false;
  }
  inventory.cursor =
      inventory.cursor.empty()
          ? result
          : normalizedStack({result.item, inventory.cursor.count + result.count,
                             result.durability});
  for (const std::size_t index : match->occupied) {
    --grid[index].count;
    grid[index] = normalizedStack(grid[index]);
  }
  return true;
}

bool addItem(InventoryState &inventory, ItemStack stack, PlayDimension mode) {
  stack = normalizedStack(stack);
  if (stack.empty() || !isStorable(stack.item)) {
    return false;
  }
  InventoryState result = inventory;
  const auto addTo = [&stack](auto &slots,
                              const std::vector<std::size_t> &indices) {
    for (const std::size_t index : indices) {
      ItemStack &slot = slots[index];
      if (stack.empty()) {
        return;
      }
      if (!slot.empty() && slot.item == stack.item && !isTool(stack.item)) {
        const int amount =
            std::min(stack.count, maximumStackSize(stack.item) - slot.count);
        slot.count += amount;
        stack.count -= amount;
        stack = normalizedStack(stack);
      }
    }
    for (const std::size_t index : indices) {
      ItemStack &slot = slots[index];
      if (stack.empty()) {
        return;
      }
      if (slot.empty()) {
        const int amount = std::min(stack.count, maximumStackSize(stack.item));
        slot = normalizedStack({stack.item, amount, stack.durability});
        stack.count -= amount;
        stack = normalizedStack(stack);
      }
    }
  };
  addTo(result.quick, visibleQuickSlots(mode));
  addTo(result.storage, visibleStorageSlots(mode));
  if (!stack.empty()) {
    return false;
  }
  inventory = std::move(result);
  return true;
}

bool consumeSelectedItem(InventoryState &inventory) {
  ItemStack &selected = inventory.quick.at(static_cast<std::size_t>(
      std::clamp(inventory.selectedQuick, 0, quickSlotCount - 1)));
  if (selected.empty()) {
    return false;
  }
  --selected.count;
  selected = normalizedStack(selected);
  return true;
}

bool damageSelectedTool(InventoryState &inventory, int amount) {
  ItemStack &selected = inventory.quick.at(static_cast<std::size_t>(
      std::clamp(inventory.selectedQuick, 0, quickSlotCount - 1)));
  if (selected.empty() || !isTool(selected.item) || amount <= 0) {
    return false;
  }
  selected.durability -= amount;
  if (selected.durability <= 0) {
    selected = {};
  }
  return true;
}

void selectQuickSlot(InventoryState &inventory, PlayDimension mode,
                     int visibleIndex) {
  const std::vector<std::size_t> visible = visibleQuickSlots(mode);
  if (visible.empty()) {
    return;
  }
  visibleIndex =
      std::clamp(visibleIndex, 0, static_cast<int>(visible.size()) - 1);
  inventory.selectedQuick = static_cast<int>(visible[visibleIndex]);
}

bool enterFourDQuickDigit(InventoryState &inventory,
                          FourDQuickSelectionState &selection, int digit,
                          std::uint64_t nowMilliseconds) {
  if (selection.pendingLayer >= 0 &&
      nowMilliseconds <= selection.deadlineMilliseconds && digit >= 1 &&
      digit <= fourDQuickLayerSlotCount) {
    inventory.selectedQuick =
        selection.pendingLayer * fourDQuickLayerSlotCount + digit - 1;
    selection = {};
    return true;
  }

  selection = {};
  if (digit >= 1 && digit <= quickSide) {
    selection.pendingLayer = digit - 1;
    selection.deadlineMilliseconds =
        nowMilliseconds + fourDQuickSequenceMilliseconds;
  }
  return false;
}

int activeFourDQuickLayer(const InventoryState &inventory,
                          const FourDQuickSelectionState &selection,
                          std::uint64_t nowMilliseconds) {
  if (selection.pendingLayer >= 0 &&
      nowMilliseconds <= selection.deadlineMilliseconds) {
    return selection.pendingLayer;
  }
  return std::clamp(inventory.selectedQuick, 0, quickSlotCount - 1) /
         fourDQuickLayerSlotCount;
}

void clickStack(ItemStack &slot, ItemStack &cursor, int mouseButton) {
  slot = normalizedStack(slot);
  cursor = normalizedStack(cursor);
  if (mouseButton == 1) {
    if (cursor.empty() || slot.empty() || cursor.item != slot.item ||
        isTool(cursor.item)) {
      std::swap(slot, cursor);
      return;
    }
    const int amount =
        std::min(cursor.count, maximumStackSize(slot.item) - slot.count);
    slot.count += amount;
    cursor.count -= amount;
    cursor = normalizedStack(cursor);
    return;
  }
  if (mouseButton != 3) {
    return;
  }
  if (cursor.empty() && !slot.empty()) {
    const int amount = (slot.count + 1) / 2;
    cursor = normalizedStack({slot.item, amount, slot.durability});
    slot.count -= amount;
    slot = normalizedStack(slot);
  } else if (!cursor.empty() &&
             (slot.empty() ||
              (slot.item == cursor.item && !isTool(cursor.item) &&
               slot.count < maximumStackSize(slot.item)))) {
    if (slot.empty()) {
      slot = normalizedStack({cursor.item, 1, cursor.durability});
    } else {
      ++slot.count;
    }
    --cursor.count;
    cursor = normalizedStack(cursor);
  }
}

} // namespace proj4d
