#pragma once

#include "hypercraft/world/block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hypercraft {

constexpr std::size_t builtin_content_definition_count = 40;
constexpr std::size_t max_content_definition_count = 256;

enum class ContentRenderModel {
    cube,
    torch,
};

struct ContentDefinition {
    BlockId id = BlockId::air;
    std::string name;
    std::string display_name;
    std::string texture;
    bool solid = false;
    bool renderable = false;
    bool placeable = false;
    bool storable = false;
    bool fluid = false;
    bool alpha_texture = false;
    bool log = false;
    bool planks = false;
    bool tool = false;
    ContentRenderModel render_model = ContentRenderModel::cube;
    int emission = 0;
    int stack_size = 256;
    int durability = 0;
    float mining_multiplier = 0.0F;
    float tool_speed = 1.0F;
};

class ContentRegistry {
public:
    static const ContentRegistry& default_registry();
    static ContentRegistry from_lua_source(std::string_view source);

    const ContentDefinition& definition(BlockId id) const;
    bool has_definition(BlockId id) const;
    BlockId id_from_name(std::string_view name) const;
    std::string_view name(BlockId id) const;
    const std::vector<ContentDefinition>& definitions() const;
    ContentRenderModel render_model(BlockId id) const noexcept
    {
        return render_models_[static_cast<std::uint8_t>(id)];
    }
    std::uint8_t emission(BlockId id) const noexcept
    {
        return emissions_[static_cast<std::uint8_t>(id)];
    }
    std::uint8_t light_transmission_cost(BlockId id) const noexcept
    {
        return light_transmission_costs_[static_cast<std::uint8_t>(id)];
    }

private:
    std::vector<ContentDefinition> definitions_;
    std::array<std::size_t, max_content_definition_count> indices_ {};
    std::array<ContentRenderModel, max_content_definition_count> render_models_ {};
    std::array<std::uint8_t, max_content_definition_count> emissions_ {};
    std::array<std::uint8_t, max_content_definition_count> light_transmission_costs_ {};
};

BlockId block_id_from_name(std::string_view name);
std::string_view block_name(BlockId block);

} // namespace hypercraft
