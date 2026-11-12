#pragma once

#include "hypercraft/world/block.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace hypercraft {

constexpr int texture_tile_size = 16;
constexpr float encoded_voxel_light_span = 0.25F;

struct TextureRegion {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
};

struct TextureAtlas {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<TextureRegion> blocks {};
    std::array<TextureRegion, 8> cracks {};
};

TextureAtlas create_block_texture_atlas();
std::string_view texture_file_stem_for_block(BlockId block);
TextureRegion texture_region_for_block(const TextureAtlas& atlas, BlockId block);
TextureRegion texture_region_for_crack_stage(const TextureAtlas& atlas, int stage);
bool texture_has_alpha(BlockId block);

inline float texture_mode_for_block(BlockId block)
{
    return texture_has_alpha(block) ? 2.0F : 1.0F;
}

inline float texture_mode_with_voxel_lighting(float texture_mode, float block_light, float sky_light)
{
    const int block = std::clamp(static_cast<int>(block_light + 0.5F), 0, 15);
    const int sky = std::clamp(static_cast<int>(sky_light + 0.5F), 0, 15);
    // Zero fractional encoding is reserved for UI and other vertices that do
    // not participate in world lighting. The +1 sentinel keeps a genuinely
    // dark voxel distinguishable without widening MeshVertex.
    const float packed = static_cast<float>(block + sky * 16 + 1) / 256.0F;
    return texture_mode + packed * encoded_voxel_light_span;
}

inline float texture_mode_with_voxel_light(float texture_mode, float light_level)
{
    return texture_mode_with_voxel_lighting(texture_mode, light_level, 0.0F);
}

inline constexpr float voxel_cave_minimum_brightness = 0.0F;

inline float base_texture_mode(float textured)
{
    return std::floor(textured + 0.5F);
}

inline int packed_voxel_lighting_from_texture_mode(float textured)
{
    const float texture_mode = base_texture_mode(textured);
    const float encoded = std::clamp(
        (textured - texture_mode) / encoded_voxel_light_span,
        0.0F,
        1.0F);
    if (encoded <= 0.0001F) {
        return -1;
    }
    return std::clamp(static_cast<int>(std::round(encoded * 256.0F - 1.0F)), 0, 255);
}

inline float voxel_lighting_brightness(float block_light, float sky_light, float world_brightness)
{
    const float block = std::clamp(block_light / 15.0F, 0.0F, 1.0F);
    const float sky = std::clamp(sky_light / 15.0F, 0.0F, 1.0F);
    const float emitted = voxel_cave_minimum_brightness +
        (1.0F - voxel_cave_minimum_brightness) * block;
    const float daylight = voxel_cave_minimum_brightness +
        (std::clamp(world_brightness, 0.0F, 1.0F) - voxel_cave_minimum_brightness) * sky;
    return std::max(emitted, daylight);
}

} // namespace hypercraft
