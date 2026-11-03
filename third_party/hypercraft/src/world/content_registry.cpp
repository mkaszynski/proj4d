#include "hypercraft/world/content_registry.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace hypercraft {
namespace {

constexpr std::string_view default_content_lua = R"lua(
return {
  { id = "air", numeric_id = 0, display_name = "Air", texture = "", solid = false, renderable = false, placeable = false, fluid = false, alpha_texture = false, stack_size = 256, mining_multiplier = 0.0 },
  { id = "grass", numeric_id = 1, display_name = "Grass", texture = "grass", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "dirt", numeric_id = 2, display_name = "Dirt", texture = "dirt", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "stone", numeric_id = 3, display_name = "Stone", texture = "stone", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 1.0 },
  { id = "wood", numeric_id = 4, display_name = "Oak Wood", texture = "wood", solid = true, renderable = true, placeable = true, log = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "leaves", numeric_id = 5, display_name = "Leaves", texture = "leaves", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.125 },
  { id = "water", numeric_id = 6, display_name = "Water", texture = "water", solid = false, renderable = true, placeable = false, fluid = true, alpha_texture = true, stack_size = 256, mining_multiplier = 0.0 },
  { id = "crafting_table", numeric_id = 7, display_name = "Crafting Table", texture = "crafting_table", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "planks", numeric_id = 8, display_name = "Oak Planks", texture = "planks", solid = true, renderable = true, placeable = true, planks = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "sticks", numeric_id = 9, display_name = "Sticks", texture = "sticks", solid = false, renderable = false, placeable = false, stack_size = 256, mining_multiplier = 0.0 },
  { id = "tin_ore", numeric_id = 10, display_name = "Tin Ore", texture = "tin_ore", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 1.0 },
  { id = "copper_ore", numeric_id = 11, display_name = "Copper Ore", texture = "copper_ore", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 1.0 },
  { id = "iron_ore", numeric_id = 12, display_name = "Iron Ore", texture = "iron_ore", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 1.0 },
  { id = "coal_ore", numeric_id = 13, display_name = "Coal", texture = "coal_ore", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 1.0 },
  { id = "chest", numeric_id = 14, display_name = "Chest", texture = "chest", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "furnace", numeric_id = 15, display_name = "Furnace", texture = "furnace", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 1.0 },
  { id = "tin_bar", numeric_id = 16, display_name = "Tin Bar", texture = "tin_bar", solid = false, renderable = false, placeable = false, stack_size = 256, mining_multiplier = 0.0 },
  { id = "copper_bar", numeric_id = 17, display_name = "Copper Bar", texture = "copper_bar", solid = false, renderable = false, placeable = false, stack_size = 256, mining_multiplier = 0.0 },
  { id = "iron_bar", numeric_id = 18, display_name = "Iron Bar", texture = "iron_bar", solid = false, renderable = false, placeable = false, stack_size = 256, mining_multiplier = 0.0 },
  { id = "wood_tool", numeric_id = 19, display_name = "Wood Tool", texture = "wood_tool", solid = false, renderable = false, placeable = false, tool = true, stack_size = 1, durability = 200, mining_multiplier = 0.0, tool_speed = 2.0 },
  { id = "stone_tool", numeric_id = 20, display_name = "Stone Tool", texture = "stone_tool", solid = false, renderable = false, placeable = false, tool = true, stack_size = 1, durability = 400, mining_multiplier = 0.0, tool_speed = 2.5 },
  { id = "tin_tool", numeric_id = 21, display_name = "Tin Tool", texture = "tin_tool", solid = false, renderable = false, placeable = false, tool = true, stack_size = 1, durability = 600, mining_multiplier = 0.0, tool_speed = 3.0 },
  { id = "copper_tool", numeric_id = 22, display_name = "Copper Tool", texture = "copper_tool", solid = false, renderable = false, placeable = false, tool = true, stack_size = 1, durability = 800, mining_multiplier = 0.0, tool_speed = 3.5 },
  { id = "iron_tool", numeric_id = 23, display_name = "Iron Tool", texture = "iron_tool", solid = false, renderable = false, placeable = false, tool = true, stack_size = 1, durability = 1000, mining_multiplier = 0.0, tool_speed = 4.0 },
  { id = "sand", numeric_id = 24, display_name = "Sand", texture = "sand", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "snow", numeric_id = 25, display_name = "Snow", texture = "snow", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "ice", numeric_id = 26, display_name = "Ice", texture = "ice", solid = true, renderable = true, placeable = true, alpha_texture = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "glass", numeric_id = 27, display_name = "Glass", texture = "glass", solid = true, renderable = true, placeable = true, alpha_texture = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "pine_wood", numeric_id = 28, display_name = "Pine Wood", texture = "pine_wood", solid = true, renderable = true, placeable = true, log = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "jungle_wood", numeric_id = 29, display_name = "Jungle Wood", texture = "jungle_wood", solid = true, renderable = true, placeable = true, log = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "pine_planks", numeric_id = 30, display_name = "Pine Planks", texture = "pine_planks", solid = true, renderable = true, placeable = true, planks = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "jungle_planks", numeric_id = 31, display_name = "Jungle Planks", texture = "jungle_planks", solid = true, renderable = true, placeable = true, planks = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "bucket", numeric_id = 32, display_name = "Bucket", texture = "bucket", solid = false, renderable = false, placeable = false, stack_size = 1, mining_multiplier = 0.0 },
  { id = "water_bucket", numeric_id = 33, display_name = "Water Bucket", texture = "water_bucket", solid = false, renderable = false, placeable = false, stack_size = 1, mining_multiplier = 0.0 },
  { id = "lava", numeric_id = 34, display_name = "Lava", texture = "lava", solid = false, renderable = true, placeable = false, fluid = true, alpha_texture = true, stack_size = 256, mining_multiplier = 0.0 },
  { id = "lava_bucket", numeric_id = 35, display_name = "Lava Bucket", texture = "lava_bucket", solid = false, renderable = false, placeable = false, stack_size = 1, mining_multiplier = 0.0 },
  { id = "orange_grass", numeric_id = 36, display_name = "Orange Grass", texture = "orange_grass", solid = true, renderable = true, placeable = true, stack_size = 256, mining_multiplier = 0.25 },
  { id = "windswept_wood", numeric_id = 37, display_name = "Windswept Wood", texture = "windswept_wood", solid = true, renderable = true, placeable = true, log = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "windswept_planks", numeric_id = 38, display_name = "Windswept Planks", texture = "windswept_planks", solid = true, renderable = true, placeable = true, planks = true, stack_size = 256, mining_multiplier = 0.5 },
  { id = "torch", numeric_id = 39, display_name = "Torch", texture = "torch", solid = false, renderable = true, placeable = true, alpha_texture = true, render_model = "torch", emission = 15, stack_size = 256, mining_multiplier = 0.0 },
}
)lua";

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string load_content_source()
{
    const std::array<std::filesystem::path, 5> candidates {
#ifdef HYPERCRAFT_ASSET_DIR
        std::filesystem::path(HYPERCRAFT_ASSET_DIR) / "scripts" / "content.lua",
#else
        std::filesystem::path(),
#endif
        std::filesystem::path("/assets/scripts/content.lua"),
        std::filesystem::path("assets/scripts/content.lua"),
        std::filesystem::path("../assets/scripts/content.lua"),
        std::filesystem::path("../../assets/scripts/content.lua"),
    };
    for (const std::filesystem::path& path : candidates) {
        if (path.empty()) {
            continue;
        }
        std::string source = read_file(path);
        if (!source.empty()) {
            return source;
        }
    }
    return std::string(default_content_lua);
}

class ContentParser {
public:
    explicit ContentParser(std::string_view source) : source_(source) {}

    std::vector<ContentDefinition> parse()
    {
        skip_ws();
        consume_word("return");
        skip_ws();
        expect('{');
        std::vector<ContentDefinition> definitions;
        while (true) {
            skip_ws();
            if (consume('}')) {
                break;
            }
            definitions.push_back(parse_definition());
            skip_ws();
            consume(',');
        }
        if (definitions.empty()) {
            fail("content.lua must define content entries");
        }
        return definitions;
    }

private:
    ContentDefinition parse_definition()
    {
        ContentDefinition definition;
        bool saw_id = false;
        bool saw_numeric_id = false;
        bool saw_display_name = false;
        bool saw_stack_size = false;
        bool saw_mining_multiplier = false;

        expect('{');
        while (true) {
            skip_ws();
            if (consume('}')) {
                break;
            }
            const std::string key = parse_identifier();
            skip_ws();
            expect('=');
            skip_ws();
            if (key == "id") {
                definition.name = parse_string();
                saw_id = true;
            } else if (key == "numeric_id") {
                const int value = parse_int();
                if (value < 0 || value >= static_cast<int>(max_content_definition_count)) {
                    fail("numeric_id out of byte-sized BlockId range");
                }
                definition.id = static_cast<BlockId>(value);
                saw_numeric_id = true;
            } else if (key == "display_name") {
                definition.display_name = parse_string();
                saw_display_name = true;
            } else if (key == "texture") {
                definition.texture = parse_string();
            } else if (key == "solid") {
                definition.solid = parse_bool();
            } else if (key == "renderable") {
                definition.renderable = parse_bool();
            } else if (key == "placeable") {
                definition.placeable = parse_bool();
            } else if (key == "storable") {
                definition.storable = parse_bool();
            } else if (key == "fluid") {
                definition.fluid = parse_bool();
            } else if (key == "alpha_texture") {
                definition.alpha_texture = parse_bool();
            } else if (key == "log") {
                definition.log = parse_bool();
            } else if (key == "planks") {
                definition.planks = parse_bool();
            } else if (key == "tool") {
                definition.tool = parse_bool();
            } else if (key == "render_model") {
                const std::string model = parse_string();
                if (model == "cube") {
                    definition.render_model = ContentRenderModel::cube;
                } else if (model == "torch") {
                    definition.render_model = ContentRenderModel::torch;
                } else {
                    fail("render_model must be cube or torch");
                }
            } else if (key == "emission") {
                definition.emission = parse_int();
            } else if (key == "stack_size") {
                definition.stack_size = parse_int();
                saw_stack_size = true;
            } else if (key == "durability") {
                definition.durability = parse_int();
            } else if (key == "mining_multiplier") {
                definition.mining_multiplier = parse_float();
                saw_mining_multiplier = true;
            } else if (key == "tool_speed") {
                definition.tool_speed = parse_float();
            } else {
                fail("unknown content key: " + key);
            }
            skip_ws();
            consume(',');
        }
        if (!saw_id || !saw_numeric_id || !saw_display_name || !saw_stack_size || !saw_mining_multiplier) {
            fail("content entry requires id, numeric_id, display_name, stack_size, and mining_multiplier");
        }
        if (definition.name.empty()) {
            fail("content id cannot be empty");
        }
        if (definition.display_name.empty() && definition.id != BlockId::air) {
            fail("display_name cannot be empty");
        }
        if (definition.stack_size <= 0 || definition.stack_size > 256) {
            fail("stack_size must be between 1 and 256");
        }
        if (definition.durability < 0) {
            fail("durability cannot be negative");
        }
        if (definition.mining_multiplier < 0.0F || !std::isfinite(definition.mining_multiplier)) {
            fail("mining_multiplier must be finite and non-negative");
        }
        if (definition.tool_speed <= 0.0F || !std::isfinite(definition.tool_speed)) {
            fail("tool_speed must be finite and positive");
        }
        if (definition.tool && definition.placeable) {
            fail("tools cannot be placeable");
        }
        if (definition.emission < 0 || definition.emission > 15) {
            fail("emission must be between 0 and 15");
        }
        if (definition.render_model != ContentRenderModel::cube &&
            (!definition.renderable || !definition.placeable || definition.solid || definition.fluid)) {
            fail("non-cube render models must be renderable, placeable, non-solid, and non-fluid");
        }
        if (definition.id != BlockId::air && !definition.fluid && (definition.placeable || !definition.renderable)) {
            definition.storable = true;
        }
        if (definition.fluid && definition.solid) {
            fail("fluids cannot be solid");
        }
        if (!definition.tool && definition.durability != 0) {
            fail("only tools may define durability");
        }
        return definition;
    }

    void skip_ws()
    {
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (c == '-' && position_ + 1 < source_.size() && source_[position_ + 1] == '-') {
                position_ += 2;
                while (position_ < source_.size() && source_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                ++position_;
                continue;
            }
            break;
        }
    }

    bool consume(char expected)
    {
        skip_ws();
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

    void consume_word(std::string_view word)
    {
        skip_ws();
        if (source_.substr(position_, word.size()) != word) {
            fail("expected " + std::string(word));
        }
        position_ += word.size();
    }

    std::string parse_identifier()
    {
        skip_ws();
        const std::size_t start = position_;
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
                ++position_;
                continue;
            }
            break;
        }
        if (start == position_) {
            fail("expected identifier");
        }
        return std::string(source_.substr(start, position_ - start));
    }

    std::string parse_string()
    {
        skip_ws();
        expect('"');
        std::string value;
        while (position_ < source_.size()) {
            const char c = source_[position_++];
            if (c == '"') {
                return value;
            }
            if (c == '\\') {
                if (position_ >= source_.size()) {
                    fail("unterminated string escape");
                }
                value.push_back(source_[position_++]);
            } else {
                value.push_back(c);
            }
        }
        fail("unterminated string");
        return {};
    }

    bool parse_bool()
    {
        skip_ws();
        if (source_.substr(position_, 4) == "true") {
            position_ += 4;
            return true;
        }
        if (source_.substr(position_, 5) == "false") {
            position_ += 5;
            return false;
        }
        fail("expected boolean");
        return false;
    }

    int parse_int()
    {
        skip_ws();
        const std::size_t start = position_;
        if (position_ < source_.size() && source_[position_] == '-') {
            ++position_;
        }
        while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
            ++position_;
        }
        int value = 0;
        const auto text = source_.substr(start, position_ - start);
        const auto* begin = text.data();
        const auto* end = text.data() + text.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc() || result.ptr != end) {
            fail("expected integer");
        }
        return value;
    }

    float parse_float()
    {
        skip_ws();
        const std::size_t start = position_;
        if (position_ < source_.size() && source_[position_] == '-') {
            ++position_;
        }
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if ((c >= '0' && c <= '9') || c == '.') {
                ++position_;
                continue;
            }
            break;
        }
        try {
            return std::stof(std::string(source_.substr(start, position_ - start)));
        } catch (const std::exception&) {
            fail("expected number");
        }
        return 0.0F;
    }

    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error("content.lua parse error at byte " + std::to_string(position_) + ": " + message);
    }

    std::string_view source_;
    std::size_t position_ = 0;
};

std::string_view expected_name_for_id(BlockId id)
{
    switch (id) {
    case BlockId::air:
        return "air";
    case BlockId::grass:
        return "grass";
    case BlockId::dirt:
        return "dirt";
    case BlockId::stone:
        return "stone";
    case BlockId::wood:
        return "wood";
    case BlockId::leaves:
        return "leaves";
    case BlockId::water:
        return "water";
    case BlockId::crafting_table:
        return "crafting_table";
    case BlockId::planks:
        return "planks";
    case BlockId::sticks:
        return "sticks";
    case BlockId::tin_ore:
        return "tin_ore";
    case BlockId::copper_ore:
        return "copper_ore";
    case BlockId::iron_ore:
        return "iron_ore";
    case BlockId::coal_ore:
        return "coal_ore";
    case BlockId::chest:
        return "chest";
    case BlockId::furnace:
        return "furnace";
    case BlockId::tin_bar:
        return "tin_bar";
    case BlockId::copper_bar:
        return "copper_bar";
    case BlockId::iron_bar:
        return "iron_bar";
    case BlockId::wood_tool:
        return "wood_tool";
    case BlockId::stone_tool:
        return "stone_tool";
    case BlockId::tin_tool:
        return "tin_tool";
    case BlockId::copper_tool:
        return "copper_tool";
    case BlockId::iron_tool:
        return "iron_tool";
    case BlockId::sand:
        return "sand";
    case BlockId::snow:
        return "snow";
    case BlockId::ice:
        return "ice";
    case BlockId::glass:
        return "glass";
    case BlockId::pine_wood:
        return "pine_wood";
    case BlockId::jungle_wood:
        return "jungle_wood";
    case BlockId::pine_planks:
        return "pine_planks";
    case BlockId::jungle_planks:
        return "jungle_planks";
    case BlockId::bucket:
        return "bucket";
    case BlockId::water_bucket:
        return "water_bucket";
    case BlockId::lava:
        return "lava";
    case BlockId::lava_bucket:
        return "lava_bucket";
    case BlockId::orange_grass:
        return "orange_grass";
    case BlockId::windswept_wood:
        return "windswept_wood";
    case BlockId::windswept_planks:
        return "windswept_planks";
    case BlockId::torch:
        return "torch";
    }
    return "unknown";
}

std::size_t index_for(BlockId id)
{
    return static_cast<std::size_t>(static_cast<std::uint8_t>(id));
}

bool is_builtin_content_id(BlockId id)
{
    return index_for(id) < builtin_content_definition_count;
}

} // namespace

ContentRegistry ContentRegistry::from_lua_source(std::string_view source)
{
    ContentRegistry registry;
    registry.indices_.fill(max_content_definition_count);
    registry.render_models_.fill(ContentRenderModel::cube);
    registry.emissions_.fill(0);
    registry.light_transmission_costs_.fill(0);
    registry.definitions_ = ContentParser(source).parse();

    if (registry.definitions_.size() < builtin_content_definition_count || registry.definitions_.size() > max_content_definition_count) {
        throw std::runtime_error("content.lua must define every stable BlockId and at most 256 total entries");
    }

    for (std::size_t index = 0; index < registry.definitions_.size(); ++index) {
        const ContentDefinition& definition = registry.definitions_[index];
        const std::size_t id_index = index_for(definition.id);
        if (registry.indices_[id_index] != max_content_definition_count) {
            throw std::runtime_error("duplicate content numeric_id: " + std::to_string(id_index));
        }
        if (is_builtin_content_id(definition.id) && definition.name != expected_name_for_id(definition.id)) {
            throw std::runtime_error(
                "content numeric_id " + std::to_string(id_index) + " must keep stable id name " +
                std::string(expected_name_for_id(definition.id)));
        }
        registry.indices_[id_index] = index;
        registry.render_models_[id_index] = definition.render_model;
        registry.emissions_[id_index] = static_cast<std::uint8_t>(definition.emission);
        registry.light_transmission_costs_[id_index] = definition.emission > 0
            ? 1
            : static_cast<std::uint8_t>(definition.alpha_texture ? 2 : (definition.solid ? 0 : 1));
    }

    for (std::size_t id = 0; id < builtin_content_definition_count; ++id) {
        if (registry.indices_[id] == max_content_definition_count) {
            throw std::runtime_error("content.lua missing stable numeric_id: " + std::to_string(id));
        }
    }

    return registry;
}

const ContentRegistry& ContentRegistry::default_registry()
{
    static const ContentRegistry registry = ContentRegistry::from_lua_source(load_content_source());
    return registry;
}

const ContentDefinition& ContentRegistry::definition(BlockId id) const
{
    const std::size_t id_index = index_for(id);
    if (id_index >= indices_.size() || indices_[id_index] >= definitions_.size()) {
        throw std::runtime_error("unknown content id");
    }
    return definitions_[indices_[id_index]];
}

bool ContentRegistry::has_definition(BlockId id) const
{
    const std::size_t id_index = index_for(id);
    return id_index < indices_.size() && indices_[id_index] < definitions_.size();
}

BlockId ContentRegistry::id_from_name(std::string_view name) const
{
    for (const ContentDefinition& definition : definitions_) {
        if (definition.name == name) {
            return definition.id;
        }
    }
    throw std::runtime_error("unknown block id: " + std::string(name));
}

std::string_view ContentRegistry::name(BlockId id) const
{
    return definition(id).name;
}

const std::vector<ContentDefinition>& ContentRegistry::definitions() const
{
    return definitions_;
}

BlockId block_id_from_name(std::string_view name)
{
    return ContentRegistry::default_registry().id_from_name(name);
}

std::string_view block_name(BlockId block)
{
    return ContentRegistry::default_registry().name(block);
}

bool is_log_block(BlockId block)
{
    return ContentRegistry::default_registry().definition(block).log;
}

bool is_planks_block(BlockId block)
{
    return ContentRegistry::default_registry().definition(block).planks;
}

bool is_fluid(BlockId block)
{
    return ContentRegistry::default_registry().definition(block).fluid;
}

bool uses_alpha_texture(BlockId block)
{
    return ContentRegistry::default_registry().definition(block).alpha_texture;
}

bool is_solid(BlockId block)
{
    return ContentRegistry::default_registry().definition(block).solid;
}

bool is_renderable(BlockId block)
{
    return ContentRegistry::default_registry().definition(block).renderable;
}

} // namespace hypercraft
