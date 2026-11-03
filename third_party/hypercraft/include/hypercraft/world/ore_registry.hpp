#pragma once

#include "hypercraft/world/block.hpp"
#include "hypercraft/world/content_registry.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace hypercraft {

struct OreGenerationRule {
    int spacing = 8;
    std::uint32_t chance = 0;
    std::uint32_t chance_denominator = 1000;
    int salt = 0;
    int patch_radius = 2;
    int min_count = 4;
    int max_count = 10;
    BlockId block = BlockId::air;
};

class OreRegistry {
public:
    static const OreRegistry& default_registry();
    static OreRegistry from_lua_source(std::string_view source);
    static OreRegistry from_lua_source(std::string_view source, const ContentRegistry& content_registry);

    const std::vector<OreGenerationRule>& rules() const;

private:
    std::vector<OreGenerationRule> rules_;
};

const std::vector<OreGenerationRule>& ore_generation_rules();

} // namespace hypercraft
