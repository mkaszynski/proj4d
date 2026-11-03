#pragma once

#include "hypercraft/world/block.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hypercraft {

class ContentRegistry;

enum class LeafShape {
    spherical,
    conal,
};

enum class TrunkShape {
    straight,
    twisted,
};

struct TreeDefinition {
    std::string id;
    int min_trunk_height = 0;
    int max_trunk_height = 0;
    BlockId trunk_material = BlockId::wood;
    TrunkShape trunk_shape = TrunkShape::straight;
    int min_leaf_radius = 0;
    int max_leaf_radius = 0;
    LeafShape leaf_shape = LeafShape::spherical;
    BlockId leaf_material = BlockId::leaves;
};

class TreeRegistry {
public:
    static const TreeRegistry& default_registry();
    static TreeRegistry from_lua_source(std::string_view source);
    static TreeRegistry from_lua_source(std::string_view source, const ContentRegistry& content_registry);

    const TreeDefinition* find(std::string_view id) const;
    const TreeDefinition& definition(std::string_view id) const;
    const std::vector<TreeDefinition>& definitions() const;
    int max_leaf_search_radius() const;

private:
    std::vector<TreeDefinition> definitions_;
    int max_leaf_search_radius_ = 0;
};

LeafShape leaf_shape_from_name(std::string_view name);
std::string_view leaf_shape_name(LeafShape shape);
TrunkShape trunk_shape_from_name(std::string_view name);
std::string_view trunk_shape_name(TrunkShape shape);

} // namespace hypercraft
