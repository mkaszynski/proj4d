#include "proj4d/content.hpp"

#include <algorithm>

#include "hypercraft/world/content_registry.hpp"

namespace proj4d {

namespace {

const hypercraft::ContentDefinition &definition(BlockId block) {
  return hypercraft::ContentRegistry::default_registry().definition(block);
}

} // namespace

bool isSolid(BlockId block) { return definition(block).solid; }

bool isKnownBlock(BlockId block) {
  return hypercraft::ContentRegistry::default_registry().has_definition(block);
}

bool isRenderable(BlockId block) { return definition(block).renderable; }

bool isPlaceable(BlockId block) { return definition(block).placeable; }

bool isStorable(BlockId block) { return definition(block).storable; }

bool isTool(BlockId block) { return definition(block).tool; }

int maximumStackSize(BlockId block) { return definition(block).stack_size; }

int maximumDurability(BlockId block) { return definition(block).durability; }

double toolSpeed(BlockId block) {
  return static_cast<double>(definition(block).tool_speed);
}

double miningMultiplier(BlockId block) {
  return static_cast<double>(definition(block).mining_multiplier);
}

std::string_view blockName(BlockId block) {
  return definition(block).display_name;
}

BlockColor blockColor(BlockId block) {
  switch (block) {
  case BlockId::air:
    return {0, 0, 0};
  case BlockId::grass:
    return {91, 181, 74};
  case BlockId::dirt:
    return {139, 96, 61};
  case BlockId::stone:
    return {139, 143, 151};
  case BlockId::wood:
    return {157, 111, 63};
  case BlockId::leaves:
    return {65, 143, 70};
  case BlockId::water:
    return {62, 126, 235};
  case BlockId::crafting_table:
    return {181, 125, 67};
  case BlockId::planks:
    return {190, 145, 83};
  case BlockId::sticks:
    return {177, 132, 76};
  case BlockId::tin_ore:
    return {177, 190, 194};
  case BlockId::copper_ore:
    return {194, 118, 78};
  case BlockId::iron_ore:
    return {184, 157, 139};
  case BlockId::coal_ore:
    return {75, 77, 82};
  case BlockId::chest:
    return {185, 126, 47};
  case BlockId::furnace:
    return {104, 107, 113};
  case BlockId::tin_bar:
    return {205, 216, 220};
  case BlockId::copper_bar:
    return {218, 130, 75};
  case BlockId::iron_bar:
    return {213, 203, 187};
  case BlockId::wood_tool:
    return {174, 127, 71};
  case BlockId::stone_tool:
    return {128, 132, 140};
  case BlockId::tin_tool:
    return {195, 210, 216};
  case BlockId::copper_tool:
    return {211, 122, 67};
  case BlockId::iron_tool:
    return {219, 213, 199};
  case BlockId::sand:
    return {226, 210, 143};
  case BlockId::snow:
    return {232, 242, 247};
  case BlockId::ice:
    return {151, 207, 235};
  case BlockId::glass:
    return {178, 220, 224};
  case BlockId::pine_wood:
    return {116, 82, 52};
  case BlockId::jungle_wood:
    return {126, 91, 54};
  case BlockId::pine_planks:
    return {156, 121, 75};
  case BlockId::jungle_planks:
    return {171, 115, 69};
  case BlockId::bucket:
    return {187, 192, 199};
  case BlockId::water_bucket:
    return {75, 139, 227};
  case BlockId::lava:
    return {246, 91, 27};
  case BlockId::lava_bucket:
    return {224, 101, 37};
  case BlockId::orange_grass:
    return {194, 121, 43};
  case BlockId::windswept_wood:
    return {111, 89, 76};
  case BlockId::windswept_planks:
    return {154, 128, 104};
  case BlockId::torch:
    return {249, 188, 68};
  }
  return {255, 0, 255};
}

BlockColor shadedBlockColor(BlockId block, int percent) {
  const BlockColor base = blockColor(block);
  const auto shade = [percent](std::uint8_t channel) {
    return static_cast<std::uint8_t>(
        std::clamp((static_cast<int>(channel) * percent + 50) / 100, 0, 255));
  };
  return {shade(base.red), shade(base.green), shade(base.blue)};
}

bool isOre(BlockId block) {
  return block == BlockId::coal_ore || block == BlockId::tin_ore ||
         block == BlockId::copper_ore || block == BlockId::iron_ore;
}

} // namespace proj4d
