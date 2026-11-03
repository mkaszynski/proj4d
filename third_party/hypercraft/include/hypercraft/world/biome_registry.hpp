#pragma once

#include "hypercraft/world/block.hpp"
#include "hypercraft/world/content_registry.hpp"
#include "hypercraft/world/terrain_generator.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hypercraft {

struct BiomeDefinition {
    std::optional<Biome> builtin_biome;
    std::string id;
    double min_temperature = 0.0;
    double max_temperature = 1.0;
    double min_precipitation = 0.0;
    double max_precipitation = 1.0;
    std::array<BlockId, 4> top_blocks {
        BlockId::dirt,
        BlockId::dirt,
        BlockId::dirt,
        BlockId::grass,
    };
    BlockId water_below = BlockId::water;
    BlockId water_top = BlockId::water;
    std::string tree_id = "none";
    std::uint32_t tree_chance = 0;
};

class BiomeRegistry {
public:
    static const BiomeRegistry& default_registry();

    static BiomeRegistry from_lua_source(std::string_view source);
    static BiomeRegistry from_lua_source(std::string_view source, const ContentRegistry& content_registry);

    const BiomeDefinition& definition(Biome biome) const;
    const BiomeDefinition& definition_for_climate(double temperature, double precipitation) const;
    Biome biome_for_climate(double temperature, double precipitation) const;
    const std::vector<BiomeDefinition>& definitions() const;

private:
    std::vector<BiomeDefinition> definitions_;
    std::array<std::size_t, 6> builtin_indices_ {};
};

const char* biome_id(Biome biome);
std::optional<Biome> builtin_biome_from_id(std::string_view id);
} // namespace hypercraft
