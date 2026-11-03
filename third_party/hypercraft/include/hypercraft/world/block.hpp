#pragma once

#include <cstdint>

namespace hypercraft {

enum class BlockId : std::uint8_t {
    air = 0,
    grass = 1,
    dirt = 2,
    stone = 3,
    wood = 4,
    leaves = 5,
    water = 6,
    crafting_table = 7,
    planks = 8,
    sticks = 9,
    tin_ore = 10,
    copper_ore = 11,
    iron_ore = 12,
    coal_ore = 13,
    chest = 14,
    furnace = 15,
    tin_bar = 16,
    copper_bar = 17,
    iron_bar = 18,
    wood_tool = 19,
    stone_tool = 20,
    tin_tool = 21,
    copper_tool = 22,
    iron_tool = 23,
    sand = 24,
    snow = 25,
    ice = 26,
    glass = 27,
    pine_wood = 28,
    jungle_wood = 29,
    pine_planks = 30,
    jungle_planks = 31,
    bucket = 32,
    water_bucket = 33,
    lava = 34,
    lava_bucket = 35,
    orange_grass = 36,
    windswept_wood = 37,
    windswept_planks = 38,
    torch = 39,
};

bool is_log_block(BlockId block);
bool is_planks_block(BlockId block);
bool is_fluid(BlockId block);
bool uses_alpha_texture(BlockId block);
bool is_solid(BlockId block);
bool is_renderable(BlockId block);

} // namespace hypercraft
