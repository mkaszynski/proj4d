#include "hypercraft/world/tree_registry.hpp"

#include "hypercraft/world/biome_registry.hpp"
#include "hypercraft/world/content_registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace hypercraft {
namespace {

constexpr std::string_view default_trees_lua = R"lua(
return {
  {
    id = "none",
    trunk_height = { min = 0, max = 0 },
    trunk_material = "air",
    trunk_shape = "straight",
    leaf_radius = { min = 0, max = 0 },
    leaf_shape = "spherical",
    leaf_material = "air",
  },
  {
    id = "oak",
    trunk_height = { min = 5, max = 7 },
    trunk_material = "wood",
    trunk_shape = "straight",
    leaf_radius = { min = 2, max = 3 },
    leaf_shape = "spherical",
    leaf_material = "leaves",
  },
  {
    id = "pine",
    trunk_height = { min = 6, max = 8 },
    trunk_material = "pine_wood",
    trunk_shape = "straight",
    leaf_radius = { min = 4, max = 4 },
    leaf_shape = "conal",
    leaf_material = "leaves",
  },
  {
    id = "jungle",
    trunk_height = { min = 3, max = 13 },
    trunk_material = "jungle_wood",
    trunk_shape = "straight",
    leaf_radius = { min = 5, max = 7 },
    leaf_shape = "spherical",
    leaf_material = "leaves",
  },
  {
    id = "windswept",
    trunk_height = { min = 5, max = 7 },
    trunk_material = "windswept_wood",
    trunk_shape = "twisted",
    leaf_radius = { min = 2, max = 3 },
    leaf_shape = "spherical",
    leaf_material = "leaves",
  },
}
)lua";

bool is_valid_tree_id(std::string_view id)
{
    if (id.empty()) {
        return false;
    }
    for (const char character : id) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '_') {
            return false;
        }
    }
    return true;
}

BlockId world_block_id_from_name(const ContentRegistry& content_registry, std::string_view name)
{
    const BlockId block = content_registry.id_from_name(name);
    if (block != BlockId::air && !content_registry.definition(block).renderable) {
        throw std::runtime_error("unknown block id: " + std::string(name));
    }
    return block;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string load_trees_source()
{
    const std::array<std::filesystem::path, 4> candidates {
#ifdef HYPERCRAFT_ASSET_DIR
        std::filesystem::path(HYPERCRAFT_ASSET_DIR) / "scripts" / "trees.lua",
#else
        std::filesystem::path("/assets/scripts/trees.lua"),
#endif
        std::filesystem::path("assets/scripts/trees.lua"),
        std::filesystem::path("../assets/scripts/trees.lua"),
        std::filesystem::path("../../assets/scripts/trees.lua"),
    };

    for (const std::filesystem::path& path : candidates) {
        const std::string source = read_text_file(path);
        if (!source.empty()) {
            return source;
        }
    }
    return std::string(default_trees_lua);
}

class TreeLuaParser {
public:
    TreeLuaParser(std::string_view source, const ContentRegistry& content_registry)
        : source_(source)
        , content_registry_(content_registry)
    {
    }

    std::vector<TreeDefinition> parse_definitions()
    {
        std::vector<TreeDefinition> definitions;
        std::unordered_set<std::string> ids;
        expect_identifier("return");
        expect('{');
        while (!consume('}')) {
            TreeDefinition definition = parse_tree();
            if (!ids.insert(definition.id).second) {
                fail("duplicate tree id");
            }
            definitions.push_back(definition);
            consume(',');
        }
        if (!ids.contains("none") || !ids.contains("oak") || !ids.contains("pine") || !ids.contains("jungle")) {
            fail("trees.lua must define none, oak, pine, and jungle");
        }
        skip_space();
        if (position_ != source_.size()) {
            fail("unexpected trailing content");
        }
        return definitions;
    }

private:
    TreeDefinition parse_tree()
    {
        TreeDefinition definition;
        bool saw_id = false;
        bool saw_trunk_height = false;
        bool saw_trunk_material = false;
        bool saw_trunk_shape = false;
        bool saw_leaf_radius = false;
        bool saw_leaf_shape = false;
        bool saw_leaf_material = false;

        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "id") {
                definition.id = parse_string();
                if (!is_valid_tree_id(definition.id)) {
                    fail("tree id must use only letters, digits, and underscores");
                }
                saw_id = true;
            } else if (key == "trunk_height") {
                parse_int_range(definition.min_trunk_height, definition.max_trunk_height, "trunk_height");
                saw_trunk_height = true;
            } else if (key == "trunk_material") {
                definition.trunk_material = world_block_id_from_name(content_registry_, parse_string());
                saw_trunk_material = true;
            } else if (key == "trunk_shape") {
                definition.trunk_shape = trunk_shape_from_name(parse_string());
                saw_trunk_shape = true;
            } else if (key == "leaf_radius") {
                parse_int_range(definition.min_leaf_radius, definition.max_leaf_radius, "leaf_radius");
                saw_leaf_radius = true;
            } else if (key == "leaf_shape") {
                definition.leaf_shape = leaf_shape_from_name(parse_string());
                saw_leaf_shape = true;
            } else if (key == "leaf_material") {
                definition.leaf_material = world_block_id_from_name(content_registry_, parse_string());
                saw_leaf_material = true;
            } else {
                fail("unknown tree key: " + key);
            }
            consume(',');
        }

        if (!saw_id || !saw_trunk_height || !saw_trunk_material || !saw_trunk_shape || !saw_leaf_radius || !saw_leaf_shape || !saw_leaf_material) {
            fail("tree is missing a required field");
        }
        if (definition.min_trunk_height < 0 || definition.min_leaf_radius < 0) {
            fail("tree heights and leaf radii must be nonnegative");
        }
        return definition;
    }

    void parse_int_range(int& min_value, int& max_value, std::string_view name)
    {
        bool saw_min = false;
        bool saw_max = false;
        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "min") {
                min_value = parse_int();
                saw_min = true;
            } else if (key == "max") {
                max_value = parse_int();
                saw_max = true;
            } else {
                fail("unknown range key: " + key);
            }
            consume(',');
        }
        if (!saw_min || !saw_max) {
            fail(std::string(name) + " requires min and max");
        }
        if (min_value > max_value) {
            fail(std::string(name) + " requires min <= max");
        }
    }

    bool consume(char expected)
    {
        skip_space();
        if (position_ < source_.size() && source_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected)
    {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    void expect_identifier(std::string_view expected)
    {
        const std::string identifier = parse_identifier();
        if (identifier != expected) {
            fail("expected " + std::string(expected));
        }
    }

    std::string parse_identifier()
    {
        skip_space();
        if (position_ >= source_.size() || (!std::isalpha(as_uchar(source_[position_])) && source_[position_] != '_')) {
            fail("expected identifier");
        }
        const std::size_t start = position_;
        while (
            position_ < source_.size() &&
            (std::isalnum(as_uchar(source_[position_])) || source_[position_] == '_')
        ) {
            ++position_;
        }
        return std::string(source_.substr(start, position_ - start));
    }

    std::string parse_string()
    {
        skip_space();
        expect('"');
        std::string value;
        while (position_ < source_.size() && source_[position_] != '"') {
            if (source_[position_] == '\\') {
                fail("escape sequences are not allowed in tree strings");
            }
            value.push_back(source_[position_]);
            ++position_;
        }
        expect('"');
        return value;
    }

    int parse_int()
    {
        skip_space();
        const std::size_t start = position_;
        if (position_ < source_.size() && source_[position_] == '+') {
            ++position_;
        }
        while (position_ < source_.size() && std::isdigit(as_uchar(source_[position_]))) {
            ++position_;
        }
        if (start == position_) {
            fail("expected integer");
        }
        const std::string text(source_.substr(start, position_ - start));
        char* end = nullptr;
        const long value = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0') {
            fail("invalid integer");
        }
        return static_cast<int>(value);
    }

    void skip_space()
    {
        while (position_ < source_.size()) {
            if (std::isspace(as_uchar(source_[position_]))) {
                ++position_;
                continue;
            }
            if (source_[position_] == '-' && position_ + 1 < source_.size() && source_[position_ + 1] == '-') {
                position_ += 2;
                while (position_ < source_.size() && source_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            break;
        }
    }

    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error("trees.lua parse error at byte " + std::to_string(position_) + ": " + message);
    }

    static unsigned char as_uchar(char value)
    {
        return static_cast<unsigned char>(value);
    }

    std::string_view source_;
    const ContentRegistry& content_registry_;
    std::size_t position_ = 0;
};

} // namespace

LeafShape leaf_shape_from_name(std::string_view name)
{
    if (name == "spherical") {
        return LeafShape::spherical;
    }
    if (name == "conal") {
        return LeafShape::conal;
    }
    throw std::runtime_error("unknown leaf shape: " + std::string(name));
}

std::string_view leaf_shape_name(LeafShape shape)
{
    switch (shape) {
    case LeafShape::spherical:
        return "spherical";
    case LeafShape::conal:
        return "conal";
    }
    return "spherical";
}

TrunkShape trunk_shape_from_name(std::string_view name)
{
    if (name == "straight") {
        return TrunkShape::straight;
    }
    if (name == "twisted") {
        return TrunkShape::twisted;
    }
    throw std::runtime_error("unknown trunk shape: " + std::string(name));
}

std::string_view trunk_shape_name(TrunkShape shape)
{
    switch (shape) {
    case TrunkShape::straight:
        return "straight";
    case TrunkShape::twisted:
        return "twisted";
    }
    return "straight";
}

TreeRegistry TreeRegistry::from_lua_source(std::string_view source)
{
    return from_lua_source(source, ContentRegistry::default_registry());
}

TreeRegistry TreeRegistry::from_lua_source(std::string_view source, const ContentRegistry& content_registry)
{
    TreeRegistry registry;
    registry.definitions_ = TreeLuaParser(source, content_registry).parse_definitions();
    for (const TreeDefinition& definition : registry.definitions_) {
        int extent = definition.leaf_shape == LeafShape::conal
            ? definition.max_leaf_radius
            : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(definition.max_leaf_radius))));
        if (definition.trunk_shape == TrunkShape::twisted) {
            extent += definition.max_trunk_height;
        }
        registry.max_leaf_search_radius_ = std::max(registry.max_leaf_search_radius_, extent);
    }
    return registry;
}

const TreeRegistry& TreeRegistry::default_registry()
{
    static const TreeRegistry registry = TreeRegistry::from_lua_source(load_trees_source());
    return registry;
}

const TreeDefinition* TreeRegistry::find(std::string_view id) const
{
    for (const TreeDefinition& definition : definitions_) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

const TreeDefinition& TreeRegistry::definition(std::string_view id) const
{
    const TreeDefinition* found = find(id);
    if (found == nullptr) {
        throw std::runtime_error("unknown tree id: " + std::string(id));
    }
    return *found;
}

const std::vector<TreeDefinition>& TreeRegistry::definitions() const
{
    return definitions_;
}

int TreeRegistry::max_leaf_search_radius() const
{
    return max_leaf_search_radius_;
}

} // namespace hypercraft
